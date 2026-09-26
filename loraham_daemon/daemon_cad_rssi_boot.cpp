#include "daemon_cad_rssi_boot.h"

#include <stdlib.h>
#include <string.h>

/* --- Boot-time CAD RSSI threshold selection ------------------------------ */

DaemonCadRssiBootValue daemon_cad_rssi_boot = { false, 0.0f };

bool daemon_parse_cad_rssi_boot(const char *arg, float *out)
{
    char *endptr;
    long v;

    if (!arg || arg[0] == '\0')
        return false;

    v = strtol(arg, &endptr, 10);
    if (*endptr != '\0' || v < -130 || v > 0)
        return false;

    if (out)
        *out = (float)v;
    return true;
}

bool daemon_set_cad_rssi_boot_global(const char *arg)
{
    float dbm = 0.0f;

    if (!daemon_parse_cad_rssi_boot(arg, &dbm))
        return false;

    daemon_cad_rssi_boot.set = true;
    daemon_cad_rssi_boot.dbm = dbm;
    return true;
}

bool daemon_cad_rssi_boot_effective(float *out)
{
    if (daemon_cad_rssi_boot.set) {
        if (out)
            *out = daemon_cad_rssi_boot.dbm;
        return true;
    }

    return false;
}

void daemon_cad_rssi_boot_reset(void)
{
    memset(&daemon_cad_rssi_boot, 0, sizeof(daemon_cad_rssi_boot));
}
