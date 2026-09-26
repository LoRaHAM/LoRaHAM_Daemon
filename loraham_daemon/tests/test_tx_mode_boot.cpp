#include "../daemon_tx_mode_boot.h"

#include <stdio.h>

/* --- Boot TX mode CLI parse + resolve tests ------------------------------ */

static int g_ok = 0;
static int g_fail = 0;

static void expect_int(const char *name, int actual, int expected)
{
    if (actual == expected) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: expected %d, got %d\n", name, expected, actual);
    }
}

static void test_parse_values(void)
{
    DaemonTxModeBoot mode = DAEMON_TX_MODE_BOOT_UNSET;

    expect_int("parse direct ok", daemon_parse_tx_mode_boot("direct", &mode), 1);
    expect_int("parse direct value", mode, DAEMON_TX_MODE_BOOT_DIRECT);

    expect_int("parse managed ok", daemon_parse_tx_mode_boot("managed", &mode), 1);
    expect_int("parse managed value", mode, DAEMON_TX_MODE_BOOT_MANAGED);

    // Case-insensitive.
    expect_int("parse DIRECT upper ok", daemon_parse_tx_mode_boot("DIRECT", &mode), 1);
    expect_int("parse DIRECT upper value", mode, DAEMON_TX_MODE_BOOT_DIRECT);
    expect_int("parse Managed mixed ok", daemon_parse_tx_mode_boot("Managed", &mode), 1);
    expect_int("parse Managed mixed value", mode, DAEMON_TX_MODE_BOOT_MANAGED);

    // Invalid values fail.
    expect_int("parse invalid fails", daemon_parse_tx_mode_boot("raw", &mode), 0);
    expect_int("parse empty fails", daemon_parse_tx_mode_boot("", &mode), 0);
    expect_int("parse null fails", daemon_parse_tx_mode_boot(NULL, &mode), 0);
}

static void test_set_applies(void)
{
    daemon_tx_mode_boot_reset();

    expect_int("set direct ok", daemon_set_tx_mode_boot_global("direct"), 1);
    expect_int("set direct effective", daemon_tx_mode_boot_effective(),
               DAEMON_TX_MODE_BOOT_DIRECT);

    expect_int("set managed ok", daemon_set_tx_mode_boot_global("managed"), 1);
    expect_int("set managed effective", daemon_tx_mode_boot_effective(),
               DAEMON_TX_MODE_BOOT_MANAGED);
}

static void test_default_is_managed(void)
{
    daemon_tx_mode_boot_reset();

    // No flag: default MANAGED.
    expect_int("default managed", daemon_tx_mode_boot_effective(),
               DAEMON_TX_MODE_BOOT_MANAGED);
}

static void test_invalid_setter_fails(void)
{
    daemon_tx_mode_boot_reset();

    expect_int("invalid setter fails",
               daemon_set_tx_mode_boot_global("bogus"), 0);
    // State unchanged → still default MANAGED.
    expect_int("invalid leaves default", daemon_tx_mode_boot_effective(),
               DAEMON_TX_MODE_BOOT_MANAGED);
}

int main(void)
{
    test_parse_values();
    test_set_applies();
    test_default_is_managed();
    test_invalid_setter_fails();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
