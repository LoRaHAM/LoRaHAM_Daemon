#include "daemon_tx_mode_boot.h"

#include <strings.h>

/* --- Boot-time TX mode selection ----------------------------------------- */

DaemonTxModeBoot daemon_tx_mode_boot = DAEMON_TX_MODE_BOOT_UNSET;

bool daemon_parse_tx_mode_boot(const char *arg, DaemonTxModeBoot *out)
{
    if (!arg)
        return false;

    if (strcasecmp(arg, "managed") == 0) {
        if (out)
            *out = DAEMON_TX_MODE_BOOT_MANAGED;
        return true;
    }

    if (strcasecmp(arg, "direct") == 0) {
        if (out)
            *out = DAEMON_TX_MODE_BOOT_DIRECT;
        return true;
    }

    return false;
}

bool daemon_set_tx_mode_boot_global(const char *arg)
{
    return daemon_parse_tx_mode_boot(arg, &daemon_tx_mode_boot);
}

DaemonTxModeBoot daemon_tx_mode_boot_effective(void)
{
    if (daemon_tx_mode_boot != DAEMON_TX_MODE_BOOT_UNSET)
        return daemon_tx_mode_boot;

    return DAEMON_TX_MODE_BOOT_MANAGED;
}

void daemon_tx_mode_boot_reset(void)
{
    daemon_tx_mode_boot = DAEMON_TX_MODE_BOOT_UNSET;
}
