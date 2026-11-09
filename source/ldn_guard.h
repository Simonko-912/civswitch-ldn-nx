#pragma once
#include <stdbool.h>
#include <switch.h>

/// Result of the pre-flight environment check.
typedef struct {
    bool detection_available; ///< The Atmosphere SM MITM query could be performed.
    bool mitm_present;        ///< A MITM is installed on ldn:u (e.g. ldn_mitm running).
} LdnGuardInfo;

/// Inspects the runtime environment before LDN is initialised.
/// This never fails hard: on unsupported systems it returns a permissive result
/// (detection_available = false), so normal operation is unaffected.
LdnGuardInfo ldn_guard_check(void);
