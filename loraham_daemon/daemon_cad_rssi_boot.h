#ifndef LORAHAM_DAEMON_CAD_RSSI_BOOT_H
#define LORAHAM_DAEMON_CAD_RSSI_BOOT_H

/* --- Boot-time CAD RSSI threshold selection ------------------------------ */
/*
 * Boot override for the CAD monitoring RSSI busy threshold (dBm). The slot is
 * "unset" until --cad-rssi sets it; unset keeps the controller default.
 * Runtime SET CADRSSI can still override afterwards.
 */

typedef struct {
    bool set;
    float dbm;
} DaemonCadRssiBootValue;

extern DaemonCadRssiBootValue daemon_cad_rssi_boot;

/* Parse an integer dBm in [-130, 0]. Returns false on invalid. */
bool daemon_parse_cad_rssi_boot(const char *arg, float *out);

/* CLI setter: parse arg and store into the boot slot. */
bool daemon_set_cad_rssi_boot_global(const char *arg);

/*
 * Resolve the boot slot. Returns true and writes *out when a threshold was
 * requested; false when unset (caller keeps the controller default).
 */
bool daemon_cad_rssi_boot_effective(float *out);

/* Reset the slot to unset (startup default / test helper). */
void daemon_cad_rssi_boot_reset(void);

#endif
