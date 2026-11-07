#ifndef CIVSWITCH_GAME_PROBE_H
#define CIVSWITCH_GAME_PROBE_H

#include <switch.h>

typedef struct {
    const char *message;
    u32 detail;
    u32 slot;
    u32 error_code;
    bool completed;
} GameProbeResult;

GameProbeResult game_join(LdnIpv4Address host);
GameProbeResult game_send(bool send_loader,
                          void (*on_progress)(unsigned sent, unsigned total));
bool game_poll(GameProbeResult *result);
void game_close(void);

#endif
