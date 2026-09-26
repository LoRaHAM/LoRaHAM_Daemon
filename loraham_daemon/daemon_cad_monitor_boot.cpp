#include "daemon_cad_monitor_boot.h"

#include <strings.h>

/* --- Boot-time CAD monitor selection ------------------------------------- */

DaemonCadMonitorBoot daemon_cad_monitor_boot = DAEMON_CAD_MONITOR_BOOT_UNSET;

bool daemon_parse_cad_monitor_boot(const char *arg, DaemonCadMonitorBoot *out)
{
    if (!arg)
        return false;

    if (strcasecmp(arg, "on") == 0) {
        if (out)
            *out = DAEMON_CAD_MONITOR_BOOT_ON;
        return true;
    }

    if (strcasecmp(arg, "off") == 0) {
        if (out)
            *out = DAEMON_CAD_MONITOR_BOOT_OFF;
        return true;
    }

    return false;
}

bool daemon_set_cad_monitor_boot_global(const char *arg)
{
    return daemon_parse_cad_monitor_boot(arg, &daemon_cad_monitor_boot);
}

bool daemon_cad_monitor_boot_effective(void)
{
    return daemon_cad_monitor_boot == DAEMON_CAD_MONITOR_BOOT_ON;
}

void daemon_cad_monitor_boot_reset(void)
{
    daemon_cad_monitor_boot = DAEMON_CAD_MONITOR_BOOT_UNSET;
}
