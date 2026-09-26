#include "daemon_high_power_boot.h"

/* --- Boot-time high-power permission -------------------------------------- */

static bool g_high_power_enabled = false;

void daemon_set_high_power_boot_global(void)
{
    g_high_power_enabled = true;
}

bool daemon_high_power_enabled(void)
{
    return g_high_power_enabled;
}

void daemon_high_power_boot_reset(void)
{
    g_high_power_enabled = false;
}
