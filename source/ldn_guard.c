#include "ldn_guard.h"
#include <switch/sf/tipc.h>

// Atmosphere SM extension command (TIPC). See Atmosphere's
// sm/impl/sm_user_interface.hpp: AtmosphereHasMitm(Out<bool>, ServiceName).
#define SM_CMD_ATMOSPHERE_HAS_MITM 65004

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

LdnGuardInfo ldn_guard_check(void) {
    LdnGuardInfo info = {0};
    info.detection_available = query_ldn_mitm(&info.mitm_present);
    return info;
}
