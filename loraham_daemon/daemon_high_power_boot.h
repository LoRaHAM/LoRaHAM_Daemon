#ifndef LORAHAM_DAEMON_HIGH_POWER_BOOT_H
#define LORAHAM_DAEMON_HIGH_POWER_BOOT_H

/* --- Boot-time high-power permission -------------------------------------- */
/*
 * The `--high-power` boot flag: permission for THIS process (one process per
 * band) to accept `POWER=20` on an SX127x board. It is a bare flag -- present
 * means on, absent means off -- and it is immutable for the life of the
 * process: no CONF command can set or clear it, so a client that pushes
 * `POWER=20` on connect gets the same refusal it gets without the flag.
 *
 * It grants permission only. It selects no power, transmits nothing, changes
 * no boot default and enforces no duty cycle. On an SX1262 board it is
 * accepted and does nothing (that chip has no restricted +20 dBm mode).
 * Why +20 dBm needs a permission at all is in config_policy.cpp.
 */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CLI setter: the flag was given. Idempotent. Production writes only at startup. */
void daemon_set_high_power_boot_global(void);

/* True once the flag was given. */
bool daemon_high_power_enabled(void);

/* Reset to OFF (startup default / test helper). */
void daemon_high_power_boot_reset(void);

#ifdef __cplusplus
}
#endif

#endif
