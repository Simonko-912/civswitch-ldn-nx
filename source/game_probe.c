#include "game_probe.h"
#include "chat_payload.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define GAME_PORT 62056
#define MAX_PACKET 1200
#define FRAME_HEADER_SIZE 13
#define MAX_FRAME_PAYLOAD (MAX_PACKET - FRAME_HEADER_SIZE)
#define MAX_SAVED 64
#define MAX_PENDING 64
#define MAX_CHAT_TEXT 1024

// Protocol timing shared with CivSwitchLDN.
#define RECEIVE_TIMEOUT_US 100000
#define CONTROL_POLL_NS 10000000ULL
#define INTRO_RETRY_NS 3000000000ULL
#define JOIN_TIMEOUT_NS 30000000000ULL
#define REGISTER_DELAY_NS 100000000ULL
#define APP_REGISTER_DELAY_NS 200000000ULL
#define SNAPSHOT_DELAY_NS 400000000ULL
#define CHAT_START_DELAY_NS 500000000ULL
#define CHAT_DELAY_NS 10000000ULL
#define MISSING_RETRY_NS 500000000ULL
#define DELIVERY_QUIET_NS 1000000000ULL
#define DELIVERY_MAX_NS 3000000000ULL
#define KEEPALIVE_NS 5000000000ULL

#define SPECIAL_SEQUENCE UINT64_MAX

typedef struct {
    bool used;
    u64 sequence;
    size_t size;
    u8 data[MAX_PACKET];
} SavedPacket;

typedef struct {
    bool used;
    u64 sequence;
    size_t size;
    u8 payload[MAX_FRAME_PAYLOAD];
} PendingPacket;

typedef enum {
    Event_None,
    Event_Welcome,
    Event_Refused,
    Event_Closed,
    Event_Kicked,
    Event_IoError,
} EventKind;

typedef struct {
    EventKind kind;
    u32 slot;
    int error;
} PacketEvent;

static SavedPacket saved_packets[MAX_SAVED];
static PendingPacket pending_packets[MAX_PENDING];
static int active_fd = -1;
static bool socket_initialized;
static struct sockaddr_in active_target;
static LdnIpv4Address active_host;
static LdnIpv4Address active_local;
static u32 active_slot;
static u64 next_sequence;
static u64 incoming_expected;
static bool incoming_fresh;
static u32 next_counter;
static u64 last_missing_tick;
static u64 last_keepalive_tick;
static u64 last_retransmit_tick;
static u64 chat_start_tick;

static void put32(u8 *p, u32 value) { memcpy(p, &value, sizeof(value)); }
static void put64(u8 *p, u64 value) { memcpy(p, &value, sizeof(value)); }
static u64 get64(const u8 *p) { u64 value; memcpy(&value, p, sizeof(value)); return value; }
static u32 get32(const u8 *p) { u32 value; memcpy(&value, p, sizeof(value)); return value; }

static PacketEvent event_none(void) {
    return (PacketEvent){Event_None, 0, 0};
}

static bool event_failed(PacketEvent event) {
    return event.kind == Event_Refused || event.kind == Event_Closed ||
           event.kind == Event_Kicked || event.kind == Event_IoError;
}

static PacketEvent merge_event(PacketEvent current, PacketEvent next) {
    if (event_failed(next)) return next;
    if (current.kind == Event_None && next.kind != Event_None) return next;
    return current;
}

static GameProbeResult result_from_event(PacketEvent event, u32 detail, u32 slot) {
    switch (event.kind) {
        case Event_Refused:
            return (GameProbeResult){"Game lobby refused the connection", detail, slot, 0, false};
        case Event_Closed:
            return (GameProbeResult){"Game lobby closed the connection", detail, slot, 0, false};
        case Event_Kicked:
            return (GameProbeResult){"Removed from the game lobby", detail, slot, 0, false};
        case Event_IoError:
            return (GameProbeResult){"Game network I/O failed", detail, slot, (u32)event.error, false};
        default:
            return (GameProbeResult){"No game network error", detail, slot, 0, false};
    }
}

static int send_exact(int fd, const struct sockaddr_in *target,
                      const u8 *data, size_t size) {
    ssize_t sent = sendto(fd, data, size, 0,
                          (const struct sockaddr *)target, sizeof(*target));
    if (sent < 0) return -1;
    if ((size_t)sent != size) {
        errno = EIO;
        return -1;
    }
    return 0;
}

static void save_packet(u64 sequence, const u8 *data, size_t size) {
    SavedPacket *saved = &saved_packets[sequence % MAX_SAVED];
    saved->used = true;
    saved->sequence = sequence;
    saved->size = size;
    memcpy(saved->data, data, size);
}

static int resend_saved(int fd, const struct sockaddr_in *target, u64 sequence) {
    SavedPacket *saved = &saved_packets[sequence % MAX_SAVED];
    if (!saved->used || saved->sequence != sequence) return 0;
    if (send_exact(fd, target, saved->data, saved->size) < 0) return -1;
    last_retransmit_tick = armGetSystemTick();
    return 1;
}

static int send_frame(int fd, const struct sockaddr_in *target, LdnIpv4Address host,
                      u64 sequence, const u8 *payload, size_t size) {
    if (size > MAX_FRAME_PAYLOAD) {
        errno = EMSGSIZE;
        return -1;
    }

    u8 packet[MAX_PACKET] = {0};
    memcpy(packet, &host.addr, 4);
    packet[4] = 'F';
    put64(packet + 5, sequence);
    memcpy(packet + FRAME_HEADER_SIZE, payload, size);
    size_t packet_size = size + FRAME_HEADER_SIZE;

    if (send_exact(fd, target, packet, packet_size) < 0) return -1;
    save_packet(sequence, packet, packet_size);
    return 0;
}

static int send_control(int fd, const struct sockaddr_in *target,
                        LdnIpv4Address host, char kind, u64 sequence) {
    u8 packet[FRAME_HEADER_SIZE];
    memcpy(packet, &host.addr, 4);
    packet[4] = (u8)kind;
    put64(packet + 5, sequence);
    return send_exact(fd, target, packet, sizeof(packet));
}

static void clear_pending(void) {
    memset(pending_packets, 0, sizeof(pending_packets));
}

static bool pending_any(void) {
    for (size_t i = 0; i < MAX_PENDING; ++i) {
        if (pending_packets[i].used) return true;
    }
    return false;
}

static bool pending_store(u64 sequence, const u8 *payload, size_t size) {
    PendingPacket *free_slot = NULL;
    for (size_t i = 0; i < MAX_PENDING; ++i) {
        PendingPacket *pending = &pending_packets[i];
        if (pending->used && pending->sequence == sequence) return true;
        if (!pending->used && free_slot == NULL) free_slot = pending;
    }
    if (free_slot == NULL || size > sizeof(free_slot->payload)) return false;
    free_slot->used = true;
    free_slot->sequence = sequence;
    free_slot->size = size;
    memcpy(free_slot->payload, payload, size);
    return true;
}

static PendingPacket *pending_find(u64 sequence) {
    for (size_t i = 0; i < MAX_PENDING; ++i) {
        if (pending_packets[i].used && pending_packets[i].sequence == sequence)
            return &pending_packets[i];
    }
    return NULL;
}

static PacketEvent describe_game_payload(const u8 *payload, size_t size) {
    if (size >= 8) {
        u64 marker = get64(payload);
        if (marker == 0x4669726552465553ULL)
            return (PacketEvent){Event_Refused, 0, 0};
        if (marker == 0x46697265b9133ab7ULL)
            return (PacketEvent){Event_Closed, 0, 0};
        if (marker == 0x4669726552504c59ULL) {
            u32 slot = size >= 44 ? get32(payload + 40) : 1;
            return (PacketEvent){Event_Welcome, slot, 0};
        }
    }

    if (size >= 28 && get32(payload + 4) == 1) {
        u32 kind = get32(payload);
        if (kind == 0x4b0) {
            u32 slot = size >= 44 ? get32(payload + 40) : 1;
            return (PacketEvent){Event_Welcome, slot, 0};
        }
        if (kind == 0x4ba) return (PacketEvent){Event_Refused, 0, 0};
        if (kind == 0x3ec) return (PacketEvent){Event_Kicked, 0, 0};
    }
    return event_none();
}

static PacketEvent process_f_packet(u64 sequence, const u8 *payload, size_t size) {
    if (sequence == SPECIAL_SEQUENCE) return describe_game_payload(payload, size);

    if (sequence == 0) {
        incoming_expected = 0;
        incoming_fresh = false;
        clear_pending();
    } else if (incoming_fresh) {
        incoming_expected = sequence;
        incoming_fresh = false;
    }

    if (sequence < incoming_expected) return event_none();
    if (sequence > incoming_expected) {
        pending_store(sequence, payload, size);
        return event_none();
    }

    PacketEvent event = describe_game_payload(payload, size);
    incoming_expected++;

    for (;;) {
        PendingPacket *pending = pending_find(incoming_expected);
        if (pending == NULL) break;
        PacketEvent next = describe_game_payload(pending->payload, pending->size);
        pending->used = false;
        incoming_expected++;
        event = merge_event(event, next);
    }
    return event;
}

static PacketEvent process_datagram(int fd, const struct sockaddr_in *target,
                                    LdnIpv4Address host, LdnIpv4Address local,
                                    const u8 *reply, size_t size) {
    if (size < 5 || memcmp(reply, &local.addr, 4) != 0) return event_none();

    char kind = (char)reply[4];
    if (kind == 'S') {
        if (size == 6 && reply[5] == 'P') {
            u8 pong[6];
            memcpy(pong, &host.addr, 4);
            pong[4] = 'S';
            pong[5] = 'p';
            if (send_exact(fd, target, pong, sizeof(pong)) < 0)
                return (PacketEvent){Event_IoError, 0, errno};
        }
        return event_none();
    }

    if ((kind != 'F' && kind != 'R' && kind != 'V') || size < FRAME_HEADER_SIZE)
        return event_none();

    u64 sequence = get64(reply + 5);
    if (kind == 'R') {
        if (resend_saved(fd, target, sequence) < 0)
            return (PacketEvent){Event_IoError, 0, errno};
        return event_none();
    }

    if (kind == 'V') {
        u64 last_sequence = next_sequence > 0 ? next_sequence - 1 : 0;
        if (sequence <= last_sequence && resend_saved(fd, target, last_sequence) < 0)
            return (PacketEvent){Event_IoError, 0, errno};
        return event_none();
    }

    return process_f_packet(sequence, reply + FRAME_HEADER_SIZE,
                            size - FRAME_HEADER_SIZE);
}

static PacketEvent receive_one(int fd, const struct sockaddr_in *target,
                               LdnIpv4Address host, LdnIpv4Address local,
                               int flags) {
    u8 reply[MAX_PACKET];
    struct sockaddr_in sender = {0};
    socklen_t sender_size = sizeof(sender);
    ssize_t size = recvfrom(fd, reply, sizeof(reply), flags,
                            (struct sockaddr *)&sender, &sender_size);
    if (size < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return event_none();
        return (PacketEvent){Event_IoError, 0, errno};
    }
    if (sender.sin_addr.s_addr != target->sin_addr.s_addr ||
        sender.sin_port != target->sin_port)
        return event_none();
    return process_datagram(fd, target, host, local, reply, (size_t)size);
}

static PacketEvent transport_timers(int fd, const struct sockaddr_in *target,
                                    LdnIpv4Address host) {
    u64 now = armGetSystemTick();
    if (pending_any() && armTicksToNs(now - last_missing_tick) >= MISSING_RETRY_NS) {
        last_missing_tick = now;
        if (send_control(fd, target, host, 'R', incoming_expected) < 0)
            return (PacketEvent){Event_IoError, 0, errno};
    }
    if (armTicksToNs(now - last_keepalive_tick) >= KEEPALIVE_NS) {
        last_keepalive_tick = now;
        if (send_control(fd, target, host, 'V', incoming_expected) < 0)
            return (PacketEvent){Event_IoError, 0, errno};
    }
    return event_none();
}

static PacketEvent poll_transport(int fd, const struct sockaddr_in *target,
                                  LdnIpv4Address host, LdnIpv4Address local) {
    PacketEvent result = event_none();
    for (int i = 0; i < 16; ++i) {
        errno = 0;
        PacketEvent event = receive_one(fd, target, host, local, MSG_DONTWAIT);
        result = merge_event(result, event);
        if (event_failed(result)) return result;
        if (event.kind == Event_None && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;
    }
    return merge_event(result, transport_timers(fd, target, host));
}

static PacketEvent wait_until(int fd, const struct sockaddr_in *target,
                              LdnIpv4Address host, LdnIpv4Address local,
                              u64 deadline_tick) {
    for (;;) {
        PacketEvent event = poll_transport(fd, target, host, local);
        if (event_failed(event)) return event;

        u64 now = armGetSystemTick();
        if (now >= deadline_tick) return event;
        u64 remaining_ns = armTicksToNs(deadline_tick - now);
        if (remaining_ns > CONTROL_POLL_NS) remaining_ns = CONTROL_POLL_NS;
        svcSleepThread((s64)remaining_ns);
    }
}

static size_t introduction(u8 *out, LdnIpv4Address host) {
    // FireCONN frame from CivSwitchLDN's protocol.py. All fields are LE.
    size_t offset = 0;
    put64(out + offset, 0x46697265434f4e4eULL); offset += 8;
    memcpy(out + offset, &host.addr, 4); offset += 8;
    offset += 4;
    offset += 4;
    memcpy(out + offset, "CivSwitch-LDN-NX", 15); offset += 64;
    memcpy(out + offset, "CivSwitch-LDN-NX", 15); offset += 64;
    put32(out + offset, 6); offset += 4;
    offset += 1;
    put32(out + offset, 0); offset += 4;
    return offset;
}

static bool build_message(u8 *out, size_t capacity, u32 type, u32 counter,
                          const u8 *body, size_t length, size_t *out_size) {
    if (capacity < 28 || length > capacity - 28) return false;
    put32(out, type);
    put32(out + 4, 1);
    put32(out + 8, 1);
    put32(out + 12, 0xffffffff);
    put32(out + 16, 0xffffffff);
    put32(out + 20, 1);
    put32(out + 24, counter);
    memcpy(out + 28, body, length);
    *out_size = length + 28;
    return true;
}

static void reset_transport_state(void) {
    memset(saved_packets, 0, sizeof(saved_packets));
    clear_pending();
    next_sequence = 1;
    incoming_expected = 0;
    incoming_fresh = true;
    next_counter = 0;
    last_missing_tick = armGetSystemTick();
    last_keepalive_tick = last_missing_tick;
    last_retransmit_tick = 0;
    chat_start_tick = 0;
}

void game_close(void) {
    if (active_fd >= 0) close(active_fd);
    active_fd = -1;
    if (socket_initialized) socketExit();
    socket_initialized = false;
    reset_transport_state();
}

bool game_poll(GameProbeResult *result) {
    if (active_fd < 0) return false;
    PacketEvent event = poll_transport(active_fd, &active_target,
                                       active_host, active_local);
    if (!event_failed(event)) return false;
    if (result != NULL) *result = result_from_event(event, 0, active_slot);
    return true;
}

GameProbeResult game_join(LdnIpv4Address host) {
    GameProbeResult result = {"No reply from game lobby", 0, 0, 0, false};
    game_close();
    reset_transport_state();

    Result rc = socketInitializeDefault();
    if (R_FAILED(rc))
        return (GameProbeResult){"socketInitialize failed", 0, 0, rc, false};
    socket_initialized = true;

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        result = (GameProbeResult){"socket() failed", 0, 0, (u32)errno, false};
        goto finish;
    }

    LdnIpv4Address local;
    LdnSubnetMask mask;
    rc = ldnGetIpv4Address(&local, &mask);
    if (R_FAILED(rc)) {
        result = (GameProbeResult){"ldnGetIpv4Address failed", 0, 0, rc, false};
        goto close_socket;
    }

    struct sockaddr_in source = {0};
    source.sin_family = AF_INET;
    source.sin_port = htons(GAME_PORT);
    source.sin_addr.s_addr = htonl(local.addr);
    if (bind(fd, (const struct sockaddr *)&source, sizeof(source)) < 0) {
        result = (GameProbeResult){"bind(local LDN IP) failed", 0, 0, (u32)errno, false};
        goto close_socket;
    }

    struct sockaddr_in target = {0};
    target.sin_family = AF_INET;
    target.sin_port = htons(GAME_PORT);
    target.sin_addr.s_addr = htonl(host.addr);
    struct timeval timeout = {.tv_sec = 0, .tv_usec = RECEIVE_TIMEOUT_US};
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        result = (GameProbeResult){"setsockopt timeout failed", 0, 0, (u32)errno, false};
        goto close_socket;
    }

    u8 intro[180] = {0};
    size_t intro_size = introduction(intro, host);
    u64 join_start_tick = armGetSystemTick();
    u64 join_deadline_tick = join_start_tick + armNsToTicks(JOIN_TIMEOUT_NS);
    u64 next_intro_tick = join_start_tick;
    int attempts = 0;
    PacketEvent welcome = event_none();

    while (armGetSystemTick() < join_deadline_tick) {
        u64 now = armGetSystemTick();
        if (attempts < 3 && now >= next_intro_tick) {
            if (send_frame(fd, &target, host, 0, intro, intro_size) < 0) {
                result = (GameProbeResult){"send(FireCONN) failed", 0, 0, (u32)errno, false};
                goto close_socket;
            }
            attempts++;
            next_intro_tick = join_start_tick + armNsToTicks(INTRO_RETRY_NS * attempts);
        }

        PacketEvent event = receive_one(fd, &target, host, local, 0);
        if (event_failed(event)) {
            result = result_from_event(event, 0, 0);
            goto close_socket;
        }
        if (event.kind == Event_Welcome) {
            welcome = event;
            break;
        }
        event = transport_timers(fd, &target, host);
        if (event_failed(event)) {
            result = result_from_event(event, 0, 0);
            goto close_socket;
        }
    }

    if (welcome.kind != Event_Welcome) goto close_socket;

    u32 slot = welcome.slot;
    u64 welcome_tick = armGetSystemTick();
    u8 packet[64] = {0};
    size_t size = 0;

    u8 register_body[16] = {0};
    put32(register_body, 5);
    memcpy(register_body + 4, "1.0.0", 5);
    PacketEvent event = wait_until(fd, &target, host, local,
                                   welcome_tick + armNsToTicks(REGISTER_DELAY_NS));
    if (event_failed(event)) {
        result = result_from_event(event, 0, slot);
        goto close_socket;
    }
    if (!build_message(packet, sizeof(packet), 0x4b8, 0,
                       register_body, 9, &size) ||
        send_frame(fd, &target, host, 1, packet, size) < 0) {
        result = (GameProbeResult){"NetRegister send failed", 0, slot, (u32)errno, false};
        goto close_socket;
    }
    next_sequence = 2;

    u8 app_body[5] = {0};
    put32(app_body, slot);
    event = wait_until(fd, &target, host, local,
                       welcome_tick + armNsToTicks(APP_REGISTER_DELAY_NS));
    if (event_failed(event)) {
        result = result_from_event(event, 0, slot);
        goto close_socket;
    }
    if (!build_message(packet, sizeof(packet), 0x4b9, 1,
                       app_body, sizeof(app_body), &size) ||
        send_frame(fd, &target, host, 2, packet, size) < 0) {
        result = (GameProbeResult){"NetAppRegister send failed", 0, slot, (u32)errno, false};
        goto close_socket;
    }
    next_sequence = 3;

    u8 snapshot_body[5] = {0};
    snapshot_body[4] = 1;
    event = wait_until(fd, &target, host, local,
                       welcome_tick + armNsToTicks(SNAPSHOT_DELAY_NS));
    if (event_failed(event)) {
        result = result_from_event(event, 0, slot);
        goto close_socket;
    }
    if (!build_message(packet, sizeof(packet), 0x4e9, 2,
                       snapshot_body, sizeof(snapshot_body), &size) ||
        send_frame(fd, &target, host, 3, packet, size) < 0) {
        result = (GameProbeResult){"Snapshot send failed", 0, slot, (u32)errno, false};
        goto close_socket;
    }

    active_fd = fd;
    active_target = target;
    active_host = host;
    active_local = local;
    active_slot = slot;
    next_sequence = 4;
    next_counter = 3;
    chat_start_tick = welcome_tick + armNsToTicks(CHAT_START_DELAY_NS);
    return (GameProbeResult){"Game lobby joined", 0, slot, 0, true};

close_socket:
    close(fd);
finish:
    socketExit();
    socket_initialized = false;
    return result;
}

static PacketEvent wait_for_delivery_quiet(u64 last_chat_tick) {
    u64 hard_deadline = last_chat_tick + armNsToTicks(DELIVERY_MAX_NS);
    for (;;) {
        PacketEvent event = poll_transport(active_fd, &active_target,
                                           active_host, active_local);
        if (event_failed(event)) return event;

        u64 quiet_start = last_chat_tick;
        if (last_retransmit_tick > quiet_start) quiet_start = last_retransmit_tick;
        u64 now = armGetSystemTick();
        if (now >= quiet_start + armNsToTicks(DELIVERY_QUIET_NS) ||
            now >= hard_deadline)
            return event_none();
        svcSleepThread((s64)CONTROL_POLL_NS);
    }
}

GameProbeResult game_send(bool send_loader,
                          void (*on_progress)(unsigned sent, unsigned total)) {
    if (active_fd < 0)
        return (GameProbeResult){"Join the game with A first", 0, 0, 0, false};

    unsigned count = send_loader ? chat_payload_count : 1;
    u64 first_chat_tick = armGetSystemTick();
    u64 last_chat_tick = 0;
    if (chat_start_tick > first_chat_tick) first_chat_tick = chat_start_tick;

    for (unsigned i = 0; i < count; ++i) {
        PacketEvent event = wait_until(active_fd, &active_target, active_host, active_local,
                                       first_chat_tick + armNsToTicks(CHAT_DELAY_NS * i));
        if (event_failed(event)) return result_from_event(event, i, active_slot);

        const char *chat_text = send_loader ? chat_payload_lines[i] : "Civ7-LDN-Hello";
        size_t text_size = strlen(chat_text);
        if (text_size > MAX_CHAT_TEXT)
            return (GameProbeResult){"Chat line exceeds safe packet size", i,
                                     active_slot, EMSGSIZE, false};

        u8 chat_body[MAX_CHAT_TEXT + 16] = {0};
        u8 packet_body[MAX_FRAME_PAYLOAD] = {0};
        put32(chat_body, active_slot);
        put32(chat_body + 4, (u32)text_size);
        memcpy(chat_body + 8, chat_text, text_size);
        put32(chat_body + 8 + text_size, 0xfffffffe);
        put32(chat_body + 12 + text_size, 0xffffffff);

        size_t packet_size = 0;
        if (!build_message(packet_body, sizeof(packet_body), 0x3f3, next_counter,
                           chat_body, 16 + text_size, &packet_size))
            return (GameProbeResult){"Chat packet build failed", i,
                                     active_slot, EMSGSIZE, false};
        if (send_frame(active_fd, &active_target, active_host, next_sequence,
                       packet_body, packet_size) < 0)
            return (GameProbeResult){"Chat send failed", i,
                                     active_slot, (u32)errno, false};

        last_chat_tick = armGetSystemTick();
        next_counter++;
        next_sequence++;
        if (on_progress) on_progress(i + 1, count);
    }

    PacketEvent event = wait_for_delivery_quiet(last_chat_tick);
    if (event_failed(event)) return result_from_event(event, count, active_slot);
    return (GameProbeResult){send_loader ? "Loader lines sent; check S2" :
                             "Test chat sent; check S2",
                             count, active_slot, 0, true};
}
