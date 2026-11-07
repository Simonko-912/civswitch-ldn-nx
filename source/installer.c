#include "installer.h"
#include "installer_payload.h"
#include "save_template_bin.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define LOADER_PORT 9026
#define MAX_SUBNET_HOSTS 512
#define SCAN_BATCH 32
#define SCAN_CONNECT_MS 250
#define LOADER_WAIT_NS 45000000000ULL
#define WIFI_WAIT_NS 30000000000ULL
#define UPLOAD_TIMEOUT_NS 30000000000ULL
#define APPLY_TIMEOUT_NS 300000000000ULL
#define RAW_CHUNK 300000U
#define RESPONSE_CAPACITY 8192

static const char base64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static InstallerResult installer_result(const char *message, u32 error,
                                        u32 ip, unsigned step,
                                        unsigned total, bool completed) {
    return (InstallerResult){message, error, ip, step, total, completed};
}

static void progress(InstallerProgress callback, const char *message,
                     unsigned step, unsigned total) {
    if (callback) callback(message, step, total);
}

static int set_nonblocking(int fd, bool enabled) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    if (enabled) flags |= O_NONBLOCK;
    else flags &= ~O_NONBLOCK;
    return fcntl(fd, F_SETFL, flags);
}

static int send_all(int fd, const char *data, size_t size) {
    while (size > 0) {
        ssize_t sent = send(fd, data, size, 0);
        if (sent < 0 && errno == EINTR) continue;
        if (sent <= 0) return -1;
        data += sent;
        size -= (size_t)sent;
    }
    return 0;
}

static int connect_ip(u32 ip, unsigned timeout_ms) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    if (set_nonblocking(fd, true) < 0) {
        close(fd);
        return -1;
    }

    struct sockaddr_in target = {0};
    target.sin_family = AF_INET;
    target.sin_port = htons(LOADER_PORT);
    target.sin_addr.s_addr = ip;
    int rc = connect(fd, (const struct sockaddr *)&target, sizeof(target));
    if (rc < 0 && errno != EINPROGRESS) {
        close(fd);
        return -1;
    }

    fd_set writes;
    FD_ZERO(&writes);
    FD_SET(fd, &writes);
    struct timeval timeout = {
        .tv_sec = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };
    rc = select(fd + 1, NULL, &writes, NULL, &timeout);
    if (rc <= 0 || !FD_ISSET(fd, &writes)) {
        if (rc == 0) errno = ETIMEDOUT;
        close(fd);
        return -1;
    }

    int error = 0;
    socklen_t error_size = sizeof(error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &error_size) < 0 || error) {
        if (error) errno = error;
        close(fd);
        return -1;
    }
    if (set_nonblocking(fd, false) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void append_response(char *response, size_t *used,
                            const char *data, size_t size) {
    if (size >= RESPONSE_CAPACITY - 1) {
        data += size - (RESPONSE_CAPACITY - 1);
        size = RESPONSE_CAPACITY - 1;
        *used = 0;
    } else if (*used + size >= RESPONSE_CAPACITY) {
        size_t keep = RESPONSE_CAPACITY / 2;
        if (keep > *used) keep = *used;
        memmove(response, response + *used - keep, keep);
        *used = keep;
    }
    memcpy(response + *used, data, size);
    *used += size;
    response[*used] = 0;
}

static int run_code_on_socket(int fd, const char *code, size_t code_size,
                              const char *success, u64 timeout_ns) {
    struct timeval send_timeout = {.tv_sec = 30, .tv_usec = 0};
    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO,
                   &send_timeout, sizeof(send_timeout)) < 0)
        return -1;
    if (send_all(fd, code, code_size) < 0 || shutdown(fd, SHUT_WR) < 0) {
        return -1;
    }

    struct timeval receive_timeout = {.tv_sec = 1, .tv_usec = 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
               &receive_timeout, sizeof(receive_timeout));
    char response[RESPONSE_CAPACITY] = {0};
    size_t used = 0;
    u64 deadline = armGetSystemTick() + armNsToTicks(timeout_ns);
    int result = 1;

    while (armGetSystemTick() < deadline && appletMainLoop()) {
        char data[1024];
        ssize_t size = recv(fd, data, sizeof(data), 0);
        if (size > 0) {
            append_response(response, &used, data, (size_t)size);
            if (strstr(response, success) != NULL) {
                result = 0;
                break;
            }
            if (strstr(response, "E: ") != NULL ||
                strstr(response, "EXCEPTION:") != NULL ||
                strstr(response, "Error in payload:") != NULL ||
                strstr(response, "ERROR in accept loop:") != NULL) {
                errno = EPROTO;
                result = -1;
                break;
            }
            continue;
        }
        if (size == 0) break;
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            continue;
        result = -1;
        break;
    }
    if (result == 1) errno = ETIMEDOUT;
    return result;
}

static int run_code(u32 ip, const char *code, size_t code_size,
                    const char *success, u64 timeout_ns) {
    int fd = connect_ip(ip, 3000);
    if (fd < 0) return -1;
    int result = run_code_on_socket(fd, code, code_size, success, timeout_ns);
    close(fd);
    return result;
}

static bool probe_loader_socket(int fd) {
    static const char probe[] = "1";
    if (set_nonblocking(fd, false) < 0) return false;
    return run_code_on_socket(fd, probe, sizeof(probe) - 1,
                              "Executed successfully", 3000000000ULL) == 0;
}

static bool connected_socket(int fd) {
    int error = 0;
    socklen_t error_size = sizeof(error);
    return getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &error_size) == 0 &&
           error == 0;
}

static u32 scan_for_loader(u32 local_ip, u32 subnet_mask,
                           InstallerProgress callback) {
    u32 local_host = ntohl(local_ip);
    u32 mask_host = ntohl(subnet_mask);
    u64 count = (u64)(~mask_host) + 1;
    if (count < 4 || count > MAX_SUBNET_HOSTS) {
        errno = ENOTSUP;
        return 0;
    }
    u32 network = local_host & mask_host;
    u32 candidates[MAX_SUBNET_HOSTS];
    unsigned candidate_count = 0;
    for (u32 offset = 1; offset < count - 1; ++offset) {
        u32 candidate = network + offset;
        if (candidate != local_host)
            candidates[candidate_count++] = htonl(candidate);
    }

    u64 deadline = armGetSystemTick() + armNsToTicks(LOADER_WAIT_NS);
    unsigned pass = 0;
    while (armGetSystemTick() < deadline && appletMainLoop()) {
        pass++;
        progress(callback, "Scanning Wi-Fi for Switch 2", pass, 0);
        for (unsigned base = 0; base < candidate_count; base += SCAN_BATCH) {
            int sockets[SCAN_BATCH];
            unsigned batch = candidate_count - base;
            if (batch > SCAN_BATCH) batch = SCAN_BATCH;
            fd_set writes;
            FD_ZERO(&writes);
            int max_fd = -1;

            for (unsigned i = 0; i < batch; ++i) {
                sockets[i] = socket(AF_INET, SOCK_STREAM, 0);
                if (sockets[i] < 0 || set_nonblocking(sockets[i], true) < 0) {
                    if (sockets[i] >= 0) close(sockets[i]);
                    sockets[i] = -1;
                    continue;
                }
                struct sockaddr_in target = {0};
                target.sin_family = AF_INET;
                target.sin_port = htons(LOADER_PORT);
                target.sin_addr.s_addr = candidates[base + i];
                int rc = connect(sockets[i], (const struct sockaddr *)&target,
                                 sizeof(target));
                if (rc < 0 && errno != EINPROGRESS) {
                    close(sockets[i]);
                    sockets[i] = -1;
                    continue;
                }
                FD_SET(sockets[i], &writes);
                if (sockets[i] > max_fd) max_fd = sockets[i];
            }

            if (max_fd >= 0) {
                struct timeval timeout = {
                    .tv_sec = 0,
                    .tv_usec = SCAN_CONNECT_MS * 1000,
                };
                select(max_fd + 1, NULL, &writes, NULL, &timeout);
            }
            for (unsigned i = 0; i < batch; ++i) {
                if (sockets[i] < 0) continue;
                bool ready = FD_ISSET(sockets[i], &writes) &&
                             connected_socket(sockets[i]);
                if (ready && probe_loader_socket(sockets[i])) {
                    u32 result = candidates[base + i];
                    close(sockets[i]);
                    for (unsigned j = i + 1; j < batch; ++j)
                        if (sockets[j] >= 0) close(sockets[j]);
                    return result;
                }
                close(sockets[i]);
            }
            if (armGetSystemTick() >= deadline || !appletMainLoop()) break;
        }
        svcSleepThread(250000000LL);
    }
    errno = ETIMEDOUT;
    return 0;
}

static size_t encode_base64(char *out, const u8 *data, size_t size) {
    size_t read = 0, written = 0;
    while (read + 3 <= size) {
        u32 value = ((u32)data[read] << 16) |
                    ((u32)data[read + 1] << 8) | data[read + 2];
        out[written++] = base64_alphabet[(value >> 18) & 63];
        out[written++] = base64_alphabet[(value >> 12) & 63];
        out[written++] = base64_alphabet[(value >> 6) & 63];
        out[written++] = base64_alphabet[value & 63];
        read += 3;
    }
    if (read < size) {
        u32 value = (u32)data[read] << 16;
        out[written++] = base64_alphabet[(value >> 18) & 63];
        if (read + 1 < size) {
            value |= (u32)data[read + 1] << 8;
            out[written++] = base64_alphabet[(value >> 12) & 63];
            out[written++] = base64_alphabet[(value >> 6) & 63];
            out[written++] = '=';
        } else {
            out[written++] = base64_alphabet[(value >> 12) & 63];
            out[written++] = '=';
            out[written++] = '=';
        }
    }
    return written;
}

static InstallerResult upload_installer(u32 ip, InstallerProgress callback) {
    static const char reset[] =
        "log('STARTING INSTALLER');window.__tpl=null;'reset'";
    if (run_code(ip, reset, sizeof(reset) - 1,
                 "Executed successfully", UPLOAD_TIMEOUT_NS) != 0)
        return installer_result("Could not reset remote installer", errno,
                                ip, 0, 0, false);

    const unsigned total = (save_template_bin_size + RAW_CHUNK - 1) / RAW_CHUNK;
    const char prefix[] = "(window.__tpl=window.__tpl||[]).push('";
    const size_t command_capacity = sizeof(prefix) + ((RAW_CHUNK + 2) / 3) * 4 + 64;
    char *command = malloc(command_capacity);
    if (!command)
        return installer_result("Not enough memory for save upload", ENOMEM,
                                ip, 0, total, false);

    size_t raw_offset = 0;
    size_t encoded_total = 0;
    for (unsigned chunk = 0; chunk < total; ++chunk) {
        size_t raw_size = save_template_bin_size - raw_offset;
        if (raw_size > RAW_CHUNK) raw_size = RAW_CHUNK;
        size_t offset = 0;
        memcpy(command, prefix, sizeof(prefix) - 1);
        offset += sizeof(prefix) - 1;
        size_t encoded = encode_base64(command + offset,
                                       save_template_bin + raw_offset, raw_size);
        offset += encoded;
        int suffix = snprintf(command + offset, command_capacity - offset,
                              "');'chunk %u/%u'", chunk + 1, total);
        if (suffix < 0 || (size_t)suffix >= command_capacity - offset) {
            free(command);
            return installer_result("Save upload command overflow", EOVERFLOW,
                                    ip, chunk, total, false);
        }
        offset += (size_t)suffix;
        progress(callback, "Uploading save template", chunk + 1, total);
        if (run_code(ip, command, offset, "Executed successfully",
                     UPLOAD_TIMEOUT_NS) != 0) {
            free(command);
            return installer_result("Save template upload failed", errno,
                                    ip, chunk, total, false);
        }
        raw_offset += raw_size;
        encoded_total += encoded;
    }
    free(command);

    if (encoded_total != installer_template_base64_length)
        return installer_result("Save template length mismatch", EINVAL,
                                ip, total, total, false);

    progress(callback, "Writing persistent trigger save", total, total);
    if (run_code(ip, installer_apply_js, sizeof(installer_apply_js) - 1,
                 "INSTALL DONE", APPLY_TIMEOUT_NS) != 0)
        return installer_result("Switch 2 rejected the save installer", errno,
                                ip, total, total, false);
    return installer_result("Persistent trigger save installed", 0,
                            ip, total, total, true);
}

InstallerResult installer_run(InstallerProgress callback) {
    progress(callback, "Restoring Wi-Fi", 0, 0);
    Result rc = nifmInitialize(NifmServiceType_User);
    if (R_FAILED(rc))
        return installer_result("nifmInitialize failed", rc, 0, 0, 0, false);

    NifmRequest request = {0};
    bool request_open = false;
    bool sockets_open = false;
    InstallerResult result = installer_result("Wi-Fi did not reconnect",
                                              ETIMEDOUT, 0, 0, 0, false);
    rc = nifmCreateRequest(&request, true);
    if (R_FAILED(rc)) {
        result = installer_result("nifmCreateRequest failed", rc, 0, 0, 0, false);
        goto finish;
    }
    request_open = true;
    rc = nifmRequestSubmit(&request);
    if (R_FAILED(rc)) {
        result = installer_result("nifmRequestSubmit failed", rc, 0, 0, 0, false);
        goto finish;
    }

    u32 local_ip = 0, mask = 0, gateway = 0, dns1 = 0, dns2 = 0;
    u64 wifi_deadline = armGetSystemTick() + armNsToTicks(WIFI_WAIT_NS);
    while (armGetSystemTick() < wifi_deadline && appletMainLoop()) {
        rc = nifmGetCurrentIpConfigInfo(&local_ip, &mask, &gateway, &dns1, &dns2);
        if (R_SUCCEEDED(rc) && local_ip != 0 && mask != 0) break;
        svcSleepThread(250000000LL);
    }
    if (local_ip == 0 || mask == 0) goto finish;

    rc = socketInitializeDefault();
    if (R_FAILED(rc)) {
        result = installer_result("socketInitialize failed", rc, 0, 0, 0, false);
        goto finish;
    }
    sockets_open = true;
    u32 switch_ip = scan_for_loader(local_ip, mask, callback);
    if (!switch_ip) {
        result = installer_result("Remote loader not found on Wi-Fi", errno,
                                  0, 0, 0, false);
        goto finish;
    }
    progress(callback, "Remote loader found", 0, 0);
    result = upload_installer(switch_ip, callback);

finish:
    if (sockets_open) socketExit();
    if (request_open) nifmRequestClose(&request);
    nifmExit();
    return result;
}
