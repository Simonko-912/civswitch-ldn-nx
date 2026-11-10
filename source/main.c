#include <switch.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include "game_probe.h"
#include "installer.h"
#include "ldn_guard.h"

// This build tests native LDN and the Civilization VII Remote JS Loader bootstrap.
#define MAX_NETWORKS 24

static LdnNetworkInfo networks[MAX_NETWORKS];
static int network_count;
static int selected;
static bool connected;
static bool game_joined;
static bool station_open;
static bool ldn_initialized;
static Result last_result;
static const char *last_operation = "Ready";
static GameProbeResult game_result;
static bool game_tested;
static InstallerResult install_result;
static bool install_tested;
// Whether the user has explicitly run a scan (pressed X). The startup
// auto-scan does not set this, so the empty-scan hint waits for a manual scan.
static bool user_scanned;

static void show_progress(unsigned sent, unsigned total) {
    consoleClear();
    printf("Sending game chat: %u/%u\n", sent, total);
    printf("Keep the Switch 2 lobby open.\n");
    consoleUpdate(NULL);
}

static void show_install_progress(const char *message, unsigned step,
                                  unsigned total) {
    consoleClear();
    printf("CivSwitch LDN NX v1.2.0 - one-click save\n\n");
    printf("%s\n", message);
    if (total) printf("Progress: %u/%u\n", step, total);
    printf("\nDo not close the game or either console.\n");
    printf("This replaces Civilization VII single-player saves.\n");
    consoleUpdate(NULL);
}

static void report(const char *operation, Result result) {
    last_operation = operation;
    last_result = result;
}

// Hard block shown whenever ldn_mitm intercepts ldn:u. There is no "continue":
// the app cannot use real local wireless while the module is resident, and
// turning it off in its config is not sufficient, so the user must delete it
// and reboot. Returns only when they choose exit.
static void guard_block(PadState *pad) {
    for (;;) {
        consoleClear();
        printf("CivSwitch LDN NX v1.2.0\n\n");
        printf("BLOCKED: ldn_mitm plugin detected\n\n");
        printf("ldn_mitm is intercepting the local\n");
        printf("wireless service. This app needs real\n");
        printf("local wireless and cannot run while\n");
        printf("ldn_mitm is active.\n");
        printf("Note: turning it off in its config is\n");
        printf("not enough.\n\n");
        printf("To use this app:\n");
        printf("1. Delete the ldn_mitm folder:\n");
        printf("   atmosphere/contents/4200000000000010\n");
        printf("2. Reboot the console.\n");
        printf("3. Start this app again.\n\n");
        printf("Press + or B to exit.\n");
        consoleUpdate(NULL);

        padUpdate(pad);
        u64 keys = padGetButtonsDown(pad);
        if (keys & (HidNpadButton_Plus | HidNpadButton_B)) return;
        svcSleepThread(20 * 1000 * 1000);
    }
}

// Application mode (hold-R title override) can scan rooms but the game-level
// handshake never reaches the host, so joining always fails - except when the
// overridden title is Civilization VII itself. Warn and stop in every other
// case. Returns only when the user chooses exit.
static void app_mode_block(PadState *pad, const LdnGuardInfo *guard) {
    for (;;) {
        consoleClear();
        printf("CivSwitch LDN NX v1.2.0\n\n");
        printf("PLEASE USE APPLET MODE\n\n");
        printf("This app is running in application\n");
        printf("mode (hold-R title override). In\n");
        printf("this mode rooms can be scanned but\n");
        printf("the game handshake cannot reach\n");
        printf("the host, so joining fails.\n\n");
        printf("Please start it from the Album\n");
        printf("(applet mode) instead.\n\n");
        if (guard->title_available)
            printf("Host title: %016llX\n",
                   (unsigned long long)guard->host_title_id);
        printf("Overriding Civilization VII\n");
        printf("itself is allowed.\n\n");
        printf("Press + or B to exit.\n");
        consoleUpdate(NULL);

        padUpdate(pad);
        u64 keys = padGetButtonsDown(pad);
        if (keys & (HidNpadButton_Plus | HidNpadButton_B)) return;
        svcSleepThread(20 * 1000 * 1000);
    }
}

static void close_station(void) {
    game_close();
    game_joined = false;
    if (connected) {
        ldnDisconnect();
        connected = false;
    }
    game_tested = false;
    if (station_open) {
        ldnCloseStation();
        station_open = false;
    }
}

// The radio can return empty placeholder entries for ordinary WiFi signals
// (zero application ID, no nodes). Those are not real rooms, so drop them.
static bool is_real_room(const LdnNetworkInfo *network) {
    return network->node_count >= 1 &&
           network->network_id.intent_id.local_communication_id != 0;
}

static void compact_networks(void) {
    int kept = 0;
    for (int i = 0; i < network_count; i++) {
        if (is_real_room(&networks[i])) {
            if (kept != i) networks[kept] = networks[i];
            kept++;
        }
    }
    network_count = kept;
    if (selected >= network_count) selected = 0;
}

static void scan_networks(void) {
    close_station();
    network_count = 0;
    selected = 0;
    Result rc = ldnOpenStation();
    report("ldnOpenStation", rc);
    if (R_FAILED(rc)) return;
    station_open = true;

    LdnScanFilter filter = {0};
    // Scanning without an application-ID filter is intentional for diagnosis.
    rc = ldnScan(0, &filter, networks, MAX_NETWORKS, &network_count);
    report("ldnScan", rc);
    if (R_FAILED(rc)) network_count = 0;
    if (network_count > MAX_NETWORKS) network_count = MAX_NETWORKS;
    // Discard empty WiFi placeholders so the count reflects real rooms only.
    compact_networks();
}

static void print_ip(LdnIpv4Address addr) {
    u32 ip = addr.addr;
    printf("%u.%u.%u.%u", (ip >> 24) & 255, (ip >> 16) & 255,
           (ip >> 8) & 255, ip & 255);
}

static void print_wifi_ip(u32 network_order_ip) {
    u32 ip = ntohl(network_order_ip);
    printf("%u.%u.%u.%u", (ip >> 24) & 255, (ip >> 16) & 255,
           (ip >> 8) & 255, ip & 255);
}

static const char *network_problem(const LdnNetworkInfo *network) {
    if (network->node_count == 0 || network->node_count > 8)
        return "Selected LDN network has no valid host";
    if (network->node_count_max <= 0 || network->node_count_max > 8)
        return "Selected LDN network has invalid capacity";
    if (network->node_count >= (u8)network->node_count_max)
        return "Selected LDN network is full";
    if (network->station_accept_policy == LdnAcceptPolicy_AlwaysReject)
        return "Selected LDN network rejects new players";
    if (network->nodes[0].local_communication_version < 0)
        return "Selected LDN network has invalid game version";
    return NULL;
}

static void connect_selected(void) {
    if (!station_open || network_count == 0 || connected) return;
    LdnNetworkInfo *network = &networks[selected];
    const char *problem = network_problem(network);
    if (problem != NULL) {
        game_result = (GameProbeResult){problem, 0, 0, 0, false};
        game_tested = true;
        return;
    }

    // The passphrase is the game's LDN value from CivSwitchLDN, not prod.keys.
    static const u8 passphrase[] = {
        0xcb, 0x96, 0x44, 0x87, 0xb4, 0x49, 0xf4, 0x20,
        0x8b, 0x1e, 0xab, 0xee, 0x6e, 0x92, 0x7f, 0x89,
        0x47, 0xfa, 0xd3, 0xd8, 0x8c, 0x37
    };
    LdnSecurityConfig security = {0};
    security.passphrase_size = sizeof(passphrase);
    memcpy(security.passphrase, passphrase, sizeof(passphrase));

    LdnUserConfig user = {0};
    strcpy(user.user_name, "CivSwitch-LDN-NX");
    s32 version = network->nodes[0].local_communication_version;
    Result rc = ldnConnect(&security, &user, version, 0, network);
    report("ldnConnect", rc);
    connected = R_SUCCEEDED(rc);
    if (connected) {
        game_result = game_join(network->nodes[0].ip_addr);
        game_tested = true;
        game_joined = game_result.completed;
    }
}

static void draw(void) {
    consoleClear();
    printf("CivSwitch LDN NX v1.2.0\n");
    printf("A join  R loader  ZR one-click save\n\n");
    printf("%s: 0x%08X\n", last_operation, last_result);
    printf("Found %d LDN network(s)\n\n", network_count);
    if (network_count > 0) {
        LdnNetworkInfo *network = &networks[selected];
        printf("Selected %d/%d (Up/Down)\n", selected + 1, network_count);
        printf("Application ID: %016llX\n", (unsigned long long)network->network_id.intent_id.local_communication_id);
        printf("Scene: %u  Channel: %d\n", network->network_id.intent_id.scene_id, network->common.channel);
        if (network->node_count > 0) {
            printf("LDN version: %u  Game version: %d\n", network->version,
                   network->nodes[0].local_communication_version);
            printf("Host: ");
            print_ip(network->nodes[0].ip_addr);
        } else {
            printf("LDN version: %u  Host unavailable", network->version);
        }
        printf("\nPlayers: %d/%d\n", network->node_count, network->node_count_max);
    } else if (user_scanned) {
        printf("If another Switch is hosting nearby:\n");
        printf("- make sure it is in the game's local\n");
        printf("  wireless room (not asleep)\n");
        printf("- keep the consoles close\n");
        printf("- press X to scan again\n");
    } else {
        printf("Press X to scan for nearby rooms.\n");
    }
    if (connected) {
        LdnIpv4Address addr;
        LdnSubnetMask mask;
        Result rc = ldnGetIpv4Address(&addr, &mask);
        printf("\nLDN connected; local IP: ");
        if (R_SUCCEEDED(rc)) print_ip(addr);
        else printf("IP error 0x%08X", rc);
        printf("\nGame lobby: %s\n", game_joined ? "JOINED" : "join failed");
    }
    if (game_tested) {
        printf("Game: %s\n", game_result.message);
        printf("Game slot: %u\n", game_result.slot);
        if (game_result.detail) printf("Chat lines sent: %u\n", game_result.detail);
        if (game_result.error_code) printf("Error code: 0x%08X\n", game_result.error_code);
        if (game_joined && !game_result.detail)
            printf("Check new player on S2. Y optional.\n");
        if (!connected && game_result.completed)
            printf("LDN closed. Check the Switch 2 loader.\n");
    }
    if (install_tested) {
        printf("\nSave installer: %s\n", install_result.message);
        if (install_result.console_ip) {
            printf("Switch 2: ");
            print_wifi_ip(install_result.console_ip);
            printf(":9026\n");
        }
        if (install_result.error_code)
            printf("Installer error: 0x%08X\n", install_result.error_code);
        if (install_result.completed)
            printf("DONE: reboot S2, then choose Load Game.\n");
    }
    printf("\nUp/Down select  X scan  A join lobby\n");
    printf("Y test  R loader only  ZR replace saves\n");
    printf("WARNING: ZR deletes all single-player saves.\n");
    printf("B leave  + exit\n");
    consoleUpdate(NULL);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    // Pre-flight environment check. If a MITM intercepts ldn:u, hard-block:
    // real LDN is unavailable while the module is resident, so we never
    // initialise LDN or scan and return to the Homebrew Menu instead.
    LdnGuardInfo guard = ldn_guard_check();
    if (guard.mitm_present) {
        guard_block(&pad);
        consoleExit(NULL);
        return 0;
    }

    // Application mode only works when overriding Civilization VII. For any
    // other host title, stop before LDN is initialised and ask for applet mode.
    if (guard.app_mode && !guard.is_civilization_vii) {
        app_mode_block(&pad, &guard);
        consoleExit(NULL);
        return 0;
    }

    Result rc = ldnInitialize(LdnServiceType_User);
    report("ldnInitialize(ldn:u)", rc);
    ldn_initialized = R_SUCCEEDED(rc);
    if (ldn_initialized) scan_networks();

    bool redraw = true;
    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 keys = padGetButtonsDown(&pad);
        if (keys & HidNpadButton_Plus) break;
        if ((keys & HidNpadButton_B) && connected) {
            close_station();
            report("ldnDisconnect", 0);
            redraw = true;
        }
        if (keys & HidNpadButton_X) {
            user_scanned = true;
            install_tested = false;
            if (ldn_initialized) scan_networks();
            else {
                rc = ldnInitialize(LdnServiceType_User);
                report("ldnInitialize(ldn:u)", rc);
                ldn_initialized = R_SUCCEEDED(rc);
                if (ldn_initialized) scan_networks();
            }
            redraw = true;
        }
        if ((keys & HidNpadButton_Y) && game_joined) {
            game_result = game_send(false, show_progress);
            if (!game_result.completed) close_station();
            game_tested = true;
            redraw = true;
        }
        if ((keys & HidNpadButton_R) && game_joined) {
            game_result = game_send(true, show_progress);
            close_station();
            game_tested = true;
            redraw = true;
        }
        if ((keys & HidNpadButton_ZR) && game_joined) {
            game_result = game_send(true, show_progress);
            bool loader_sent = game_result.completed;
            close_station();
            game_tested = true;
            install_tested = false;
            if (ldn_initialized) {
                ldnExit();
                ldn_initialized = false;
            }
            network_count = 0;
            user_scanned = false;
            if (loader_sent) {
                install_result = installer_run(show_install_progress);
                install_tested = true;
            }
            redraw = true;
        }
        if (!game_joined && network_count > 0) {
            if (keys & HidNpadButton_Down) {
                selected = (selected + 1) % network_count;
                redraw = true;
            }
            if (keys & HidNpadButton_Up) {
                selected = (selected + network_count - 1) % network_count;
                redraw = true;
            }
            if (keys & HidNpadButton_A) {
                if (!connected) connect_selected();
                else {
                    game_result = game_join(networks[selected].nodes[0].ip_addr);
                    game_tested = true;
                    game_joined = game_result.completed;
                }
                redraw = true;
            }
        }
        if (game_joined) {
            GameProbeResult poll_result;
            if (game_poll(&poll_result)) {
                game_result = poll_result;
                close_station();
                game_tested = true;
                redraw = true;
            }
        }
        if (redraw) {
            draw();
            redraw = false;
        }
        svcSleepThread(20 * 1000 * 1000);
    }
    close_station();
    if (ldn_initialized) ldnExit();
    consoleExit(NULL);
    return 0;
}
