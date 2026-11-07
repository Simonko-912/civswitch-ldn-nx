#include "game_probe.h"
#include "chat_payload.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>

#define GAME_PORT 62056
#define MAX_PACKET 1200
#define MAX_SAVED 64

// Keep the native sender's protocol timing in sync with CivSwitchLDN.
#define RECEIVE_TIMEOUT_US 100000
#define INTRO_RETRY_NS 3000000000ULL
#define JOIN_TIMEOUT_NS 30000000000ULL
#define REGISTER_DELAY_NS 100000000ULL
#define APP_REGISTER_DELAY_NS 200000000ULL
#define SNAPSHOT_DELAY_NS 400000000ULL
#define CHAT_START_DELAY_NS 500000000ULL
#define CHAT_DELAY_NS 10000000ULL
#define DELIVERY_GRACE_NS 1000000000ULL
#define KEEPALIVE_NS 5000000000ULL

static u8 saved_packets[MAX_SAVED][MAX_PACKET];
static size_t saved_sizes[MAX_SAVED];
static int active_fd = -1;
static bool socket_initialized;
static struct sockaddr_in active_target;
static LdnIpv4Address active_host;
static LdnIpv4Address active_local;
static u32 active_slot;
static u64 next_sequence;
static u64 incoming_expected;
static u32 next_counter;
static u64 last_keepalive_tick;
static u64 chat_start_tick;

static void put32(u8 *p, u32 value) { memcpy(p, &value, 4); }
static void put64(u8 *p, u64 value) { memcpy(p, &value, 8); }
static u64 get64(const u8 *p) { u64 value; memcpy(&value, p, 8); return value; }
static u32 get32(const u8 *p) { u32 value; memcpy(&value, p, 4); return value; }

static int send_frame(int fd, const struct sockaddr_in *target, LdnIpv4Address host,
                      u64 sequence, const u8 *payload, size_t size) {
    u8 packet[MAX_PACKET] = {0};
    if (size + 13 > sizeof(packet)) return -1;
    memcpy(packet, &host.addr, 4);
    packet[4] = 'F';
    put64(packet + 5, sequence);
    memcpy(packet + 13, payload, size);
    size_t packet_size = size + 13;
    if (sequence < MAX_SAVED) {
        memcpy(saved_packets[sequence], packet, packet_size);
        saved_sizes[sequence] = packet_size;
    }
    return sendto(fd, packet, packet_size, 0,
                  (const struct sockaddr *)target, sizeof(*target));
}

static void receive_control(int fd, const struct sockaddr_in *target,
                            LdnIpv4Address host, LdnIpv4Address local, u64 last_sequence) {
    // Control traffic must never stall the scheduled chat delivery.
    for (int i = 0; i < 16; ++i) {
        u8 reply[256];
        struct sockaddr_in sender = {0};
        socklen_t sender_size = sizeof(sender);
        ssize_t n = recvfrom(fd, reply, sizeof(reply), MSG_DONTWAIT,
                             (struct sockaddr *)&sender, &sender_size);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        if (n < 5 || sender.sin_addr.s_addr != target->sin_addr.s_addr ||
            sender.sin_port != target->sin_port || memcmp(reply, &local.addr, 4) != 0) continue;

        if (reply[4] == 'S' && n == 6 && reply[5] == 'P') {
            u8 pong[6];
            memcpy(pong, &host.addr, 4);
            pong[4] = 'S';
            pong[5] = 'p';
            sendto(fd, pong, sizeof(pong), 0, (const struct sockaddr *)target, sizeof(*target));
        }
        if (n >= 13 && reply[4] == 'F') {
            u64 sequence = get64(reply + 5);
            if (sequence == 0 || sequence >= incoming_expected)
                incoming_expected = sequence + 1;
        }
        if (n >= 13 && (reply[4] == 'R' || reply[4] == 'V')) {
            u64 sequence = reply[4] == 'R' ? get64(reply + 5) : last_sequence;
            if (sequence < MAX_SAVED && saved_sizes[sequence]) {
                sendto(fd, saved_packets[sequence], saved_sizes[sequence], 0,
                       (const struct sockaddr *)target, sizeof(*target));
            }
        }
    }
}

static void wait_with_control_until(u64 deadline_tick) {
    while (active_fd >= 0) {
        receive_control(active_fd, &active_target, active_host, active_local,
                        next_sequence - 1);
        u64 now = armGetSystemTick();
        if (now >= deadline_tick) return;
        u64 remaining_ns = armTicksToNs(deadline_tick - now);
        svcSleepThread((s64)remaining_ns);
    }
}

static void wait_handshake_until(int fd, const struct sockaddr_in *target,
                                 LdnIpv4Address host, LdnIpv4Address local,
                                 u64 last_sequence, u64 deadline_tick) {
    for (;;) {
        receive_control(fd, target, host, local, last_sequence);
        u64 now = armGetSystemTick();
        if (now >= deadline_tick) return;
        u64 remaining_ns = armTicksToNs(deadline_tick - now);
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

static size_t message(u8 *out, u32 type, u32 counter, const u8 *body, size_t length) {
    put32(out, type);
    put32(out + 4, 1);
    put32(out + 8, 1);
    put32(out + 12, 0xffffffff);
    put32(out + 16, 0xffffffff);
    put32(out + 20, 1);
    put32(out + 24, counter);
    memcpy(out + 28, body, length);
    return length + 28;
}

void game_close(void) {
    if (active_fd >= 0) close(active_fd);
    active_fd = -1;
    if (socket_initialized) socketExit();
    socket_initialized = false;
    incoming_expected = 0;
    chat_start_tick = 0;
    memset(saved_sizes, 0, sizeof(saved_sizes));
}

void game_poll(void) {
    if (active_fd < 0) return;
    receive_control(active_fd, &active_target, active_host, active_local, next_sequence - 1);
    u64 now = armGetSystemTick();
    if (armTicksToNs(now - last_keepalive_tick) < KEEPALIVE_NS) return;
    last_keepalive_tick = now;
    u8 packet[13];
    memcpy(packet, &active_host.addr, 4);
    packet[4] = 'V';
    put64(packet + 5, incoming_expected);
    sendto(active_fd, packet, sizeof(packet), 0,
           (const struct sockaddr *)&active_target, sizeof(active_target));
}

GameProbeResult game_join(LdnIpv4Address host) {
    GameProbeResult result = {"No reply from game lobby", 0, 0};
    game_close();
    memset(saved_sizes, 0, sizeof(saved_sizes));
    Result rc = socketInitializeDefault();
    if (R_FAILED(rc)) return (GameProbeResult){"socketInitialize failed", rc, 0};
    socket_initialized = true;

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        result = (GameProbeResult){"socket() failed", (u32)errno, 0};
        goto finish;
    }

    LdnIpv4Address local;
    LdnSubnetMask mask;
    rc = ldnGetIpv4Address(&local, &mask);
    if (R_FAILED(rc)) {
        result = (GameProbeResult){"ldnGetIpv4Address failed", rc, 0};
        goto close_socket;
    }

    struct sockaddr_in source = {0};
    source.sin_family = AF_INET;
    source.sin_port = htons(GAME_PORT);
    source.sin_addr.s_addr = htonl(local.addr);
    if (bind(fd, (const struct sockaddr *)&source, sizeof(source)) < 0) {
        result = (GameProbeResult){"bind(local LDN IP) failed", (u32)errno, 0};
        goto close_socket;
    }

    struct sockaddr_in target = {0};
    target.sin_family = AF_INET;
    target.sin_port = htons(GAME_PORT);
    target.sin_addr.s_addr = htonl(host.addr);
    struct timeval timeout = {.tv_sec = 0, .tv_usec = RECEIVE_TIMEOUT_US};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    u8 intro[180] = {0};
    size_t intro_size = introduction(intro, host);
    u8 reply[512];
    u64 join_start_tick = armGetSystemTick();
    u64 join_deadline_tick = join_start_tick + armNsToTicks(JOIN_TIMEOUT_NS);
    u64 next_intro_tick = join_start_tick;
    int attempts = 0;
    while (armGetSystemTick() < join_deadline_tick) {
        u64 now = armGetSystemTick();
        if (attempts < 3 && now >= next_intro_tick) {
            if (send_frame(fd, &target, host, 0, intro, intro_size) < 0) {
                result = (GameProbeResult){"send(FireCONN) failed", (u32)errno, 0};
                goto close_socket;
            }
            attempts++;
            next_intro_tick = join_start_tick + armNsToTicks(INTRO_RETRY_NS * attempts);
        }
        struct sockaddr_in sender = {0};
        socklen_t sender_size = sizeof(sender);
        ssize_t n = recvfrom(fd, reply, sizeof(reply), 0,
                             (struct sockaddr *)&sender, &sender_size);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
            result = (GameProbeResult){"recvfrom failed", (u32)errno, 0};
            goto close_socket;
        }
        if (sender.sin_addr.s_addr != target.sin_addr.s_addr ||
            sender.sin_port != target.sin_port || n < 21 ||
            memcmp(reply, &local.addr, 4) != 0 || reply[4] != 'F') continue;

        u8 *body = reply + 13;
        size_t body_size = (size_t)n - 13;
        if (get64(body) == 0x4669726552465553ULL ||
            get64(body) == 0x46697265b9133ab7ULL) {
            result = (GameProbeResult){"Game lobby refused/closed", 0, 0};
            goto close_socket;
        }
        if (get64(body) != 0x4669726552504c59ULL &&
            !(body_size >= 28 && get32(body) == 0x4b0)) continue;

        u32 slot = body_size >= 44 ? get32(body + 40) : 1;
        result = (GameProbeResult){"Game welcome received", 0, slot};
        u64 welcome_tick = armGetSystemTick();
        incoming_expected = get64(reply + 5) + 1;

        u8 packet[64] = {0};
        u8 register_body[16] = {0};
        put32(register_body, 5);
        memcpy(register_body + 4, "1.0.0", 5);
        wait_handshake_until(fd, &target, host, local, 0,
                             welcome_tick + armNsToTicks(REGISTER_DELAY_NS));
        size_t size = message(packet, 0x4b8, 0, register_body, 9);
        if (send_frame(fd, &target, host, 1, packet, size) < 0) {
            result = (GameProbeResult){"NetRegister send failed", (u32)errno, slot};
            break;
        }

        u8 app_body[5] = {0};
        put32(app_body, slot);
        wait_handshake_until(fd, &target, host, local, 1,
                             welcome_tick + armNsToTicks(APP_REGISTER_DELAY_NS));
        size = message(packet, 0x4b9, 1, app_body, sizeof(app_body));
        if (send_frame(fd, &target, host, 2, packet, size) < 0) {
            result = (GameProbeResult){"NetAppRegister send failed", (u32)errno, slot};
            break;
        }

        u8 snapshot_body[5] = {0};
        snapshot_body[4] = 1;
        wait_handshake_until(fd, &target, host, local, 2,
                             welcome_tick + armNsToTicks(SNAPSHOT_DELAY_NS));
        size = message(packet, 0x4e9, 2, snapshot_body, sizeof(snapshot_body));
        if (send_frame(fd, &target, host, 3, packet, size) < 0) {
            result = (GameProbeResult){"Snapshot send failed", (u32)errno, slot};
            break;
        }
        active_fd = fd;
        active_target = target;
        active_host = host;
        active_local = local;
        active_slot = slot;
        next_sequence = 4;
        next_counter = 3;
        chat_start_tick = welcome_tick + armNsToTicks(CHAT_START_DELAY_NS);
        last_keepalive_tick = join_start_tick;
        result = (GameProbeResult){"Game lobby joined", 0, slot, true};
        return result;
    }

close_socket:
    close(fd);
finish:
    socketExit();
    socket_initialized = false;
    return result;
}

GameProbeResult game_send(bool send_loader,
                          void (*on_progress)(unsigned sent, unsigned total)) {
    if (active_fd < 0) return (GameProbeResult){"Join the game with A first", 0, 0};
    unsigned count = send_loader ? chat_payload_count : 1;
    GameProbeResult result = {"No chat sent", 0, active_slot};
    u64 first_chat_tick = armGetSystemTick();
    u64 last_chat_tick = 0;
    if (chat_start_tick > first_chat_tick) first_chat_tick = chat_start_tick;

    for (unsigned i = 0; i < count; ++i) {
        wait_with_control_until(first_chat_tick + armNsToTicks(CHAT_DELAY_NS * i));
        const char *chat_text = send_loader ? chat_payload_lines[i] : "Civ7-LDN-Hello";
        size_t text_size = strlen(chat_text);
        u8 chat_body[1040] = {0};
        u8 packet_body[1080] = {0};
        put32(chat_body, active_slot);
        put32(chat_body + 4, text_size);
        memcpy(chat_body + 8, chat_text, text_size);
        put32(chat_body + 8 + text_size, 0xfffffffe);
        put32(chat_body + 12 + text_size, 0xffffffff);
        size_t size = message(packet_body, 0x3f3, next_counter, chat_body, 16 + text_size);
        if (send_frame(active_fd, &active_target, active_host, next_sequence,
                       packet_body, size) < 0) {
            result = (GameProbeResult){"Chat send failed", (u32)errno, active_slot};
            return result;
        }
        last_chat_tick = armGetSystemTick();
        next_counter++;
        next_sequence++;
        result = (GameProbeResult){send_loader ? "Loader lines sent; check S2" :
                                   "Test chat sent; check S2", i + 1, active_slot};
        if (on_progress) on_progress(i + 1, count);
    }
    wait_with_control_until(last_chat_tick + armNsToTicks(DELIVERY_GRACE_NS));
    result.completed = true;
    return result;
}
