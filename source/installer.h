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

// Runs the persistent-save install over the LDN link to `host` (the joined
// lobby's host address, from LdnNetworkInfo.nodes[0].ip_addr). The caller
// must keep the LDN station and the lobby socket open until this returns.
InstallerResult installer_run(InstallerProgress on_progress,
                              LdnIpv4Address host);

#endif
