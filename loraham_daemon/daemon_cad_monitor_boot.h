#ifndef LORAHAM_DAEMON_CAD_MONITOR_BOOT_H
#define LORAHAM_DAEMON_CAD_MONITOR_BOOT_H

/* --- Boot-time CAD monitor selection ------------------------------------- */
/*
 * Boot setting for the CAD=0/1 monitoring indicator, for legacy CONF clients
 * that expect CAD broadcasts but cannot issue SET CADMONITOR=1. The slot is
 * "unset" until --cad-monitor sets it; apply time resolves unset to OFF.
 * Runtime SET CADMONITOR can still override afterwards.
 */

typedef enum {
    DAEMON_CAD_MONITOR_BOOT_UNSET = 0,
    DAEMON_CAD_MONITOR_BOOT_OFF,
    DAEMON_CAD_MONITOR_BOOT_ON
} DaemonCadMonitorBoot;

extern DaemonCadMonitorBoot daemon_cad_monitor_boot;

/* Parse "on"|"off" (case-insensitive). Returns false on invalid. */
bool daemon_parse_cad_monitor_boot(const char *arg, DaemonCadMonitorBoot *out);

/* CLI setter: parse arg and store into the boot slot. */
bool daemon_set_cad_monitor_boot_global(const char *arg);

/* Resolve the boot slot: value ?? OFF. Returns true when monitoring on. */
bool daemon_cad_monitor_boot_effective(void);

/* Reset the slot to UNSET (startup default / test helper). */
void daemon_cad_monitor_boot_reset(void);

#endif
