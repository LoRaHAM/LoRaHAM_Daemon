#ifndef LORAHAM_DAEMON_TX_MODE_BOOT_H
#define LORAHAM_DAEMON_TX_MODE_BOOT_H

/* --- Boot-time TX mode selection ----------------------------------------- */
/*
 * Boot TX mode requested on the command line for the selected band. The slot
 * is "unset" until --tx-mode sets it; apply time resolves unset to MANAGED.
 */

typedef enum {
    DAEMON_TX_MODE_BOOT_UNSET = 0,
    DAEMON_TX_MODE_BOOT_MANAGED,
    DAEMON_TX_MODE_BOOT_DIRECT
} DaemonTxModeBoot;

extern DaemonTxModeBoot daemon_tx_mode_boot;

/* Parse "direct"|"managed" (case-insensitive). Returns false on invalid. */
bool daemon_parse_tx_mode_boot(const char *arg, DaemonTxModeBoot *out);

/* CLI setter: parse arg and store into the boot slot. */
bool daemon_set_tx_mode_boot_global(const char *arg);

/* Resolve the boot slot: value ?? MANAGED. Never returns UNSET. */
DaemonTxModeBoot daemon_tx_mode_boot_effective(void);

/* Reset the slot to UNSET (startup default / test helper). */
void daemon_tx_mode_boot_reset(void);

#endif
