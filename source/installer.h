#ifndef CIVSWITCH_INSTALLER_H
#define CIVSWITCH_INSTALLER_H

#include <switch.h>

typedef struct {
    const char *message;
    u32 error_code;
    u32 console_ip;
    unsigned step;
    unsigned total;
    bool completed;
} InstallerResult;

typedef void (*InstallerProgress)(const char *message, unsigned step,
                                  unsigned total);

// LDN-only: the Switch 2 loader is reachable on its local-wireless IP,
// which the caller learned from the lobby node list.
// Writes the persistent trigger save (+ sdmc:/switch/payload.js when set).
InstallerResult installer_run(u32 host_ip, InstallerProgress on_progress);

// Send sdmc:/switch/payload.js to the loader only; keeps the installed
// persistent trigger save untouched (ZL).
InstallerResult installer_send_payload(u32 host_ip,
                                       InstallerProgress on_progress);

#endif
