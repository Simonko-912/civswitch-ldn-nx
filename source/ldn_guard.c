#include "ldn_guard.h"
#include <switch/sf/tipc.h>

// Atmosphere SM extension command (TIPC). See Atmosphere's
// sm/impl/sm_user_interface.hpp: AtmosphereHasMitm(Out<bool>, ServiceName).
#define SM_CMD_ATMOSPHERE_HAS_MITM 65004

// Civilization VII (Nintendo Switch) program IDs. The base game runs under the
// ...C000 ID; ...C800 is its update/patch. A title override launches the base
// program, but both are accepted so a fully patched host is still recognised.
#define CIV7_TITLE_ID_BASE   0x0100C3601518C000ULL
#define CIV7_TITLE_ID_UPDATE 0x0100C3601518C800ULL

// Read-only query: does a MITM currently intercept the ldn:u service?
// This submits no network requests and does not change any Wi-Fi state.
static bool query_ldn_mitm(bool *out_mitm) {
    *out_mitm = false;

    // The TIPC SM session exists only on 12.0.0+ or Atmosphere, which is the
    // same boundary where modern ldn_mitm releases run.
    if (!hosversionIsAtmosphere() && !hosversionAtLeast(12, 0, 0))
        return false;

    TipcService *sm = smGetServiceSessionTipc();
    if (sm == NULL) return false;

    SmServiceName name = smEncodeName("ldn:u");
    u8 has_mitm = 0;
    Result rc = tipcDispatchInOut(sm, SM_CMD_ATMOSPHERE_HAS_MITM,
                                 name, has_mitm);
    if (R_FAILED(rc)) return false;

    *out_mitm = has_mitm != 0;
    return true;
}

static bool is_civilization_vii_title(u64 title_id) {
    return title_id == CIV7_TITLE_ID_BASE || title_id == CIV7_TITLE_ID_UPDATE;
}

// In application (title override) mode the homebrew runs as the title it
// replaced, so the current application process reports that game's program ID.
// pm:dmnt is a debug service available under Atmosphere. Returns false if the
// service is unavailable or the process cannot be identified.
static bool query_host_title(u64 *out_title_id) {
    *out_title_id = 0;

    if (R_FAILED(pmdmntInitialize())) return false;

    u64 pid = 0;
    Result rc = pmdmntGetApplicationProcessId(&pid);
    if (R_SUCCEEDED(rc)) rc = pmdmntGetProgramId(out_title_id, pid);

    pmdmntExit();
    return R_SUCCEEDED(rc);
}

LdnGuardInfo ldn_guard_check(void) {
    LdnGuardInfo info = {0};
    info.detection_available = query_ldn_mitm(&info.mitm_present);

    // The Album launches homebrew as a LibraryApplet. A hold-R title override
    // runs it as an Application (or SystemApplication for system titles).
    AppletType type = appletGetAppletType();
    info.app_mode = (type == AppletType_Application ||
                     type == AppletType_SystemApplication);

    if (info.app_mode) {
        info.title_available = query_host_title(&info.host_title_id);
        if (info.title_available)
            info.is_civilization_vii = is_civilization_vii_title(info.host_title_id);
    }

    return info;
}
