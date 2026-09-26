#include "../daemon_cad_rssi_boot.h"

#include <stdio.h>

/* --- Boot CAD RSSI threshold CLI parse + resolve tests -------------------- */

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
    float v = 0.0f;

    expect_int("parse -90 ok", daemon_parse_cad_rssi_boot("-90", &v), 1);
    expect_int("parse -90 value", (int)v, -90);

    expect_int("parse 0 ok", daemon_parse_cad_rssi_boot("0", &v), 1);
    expect_int("parse 0 value", (int)v, 0);

    expect_int("parse -130 ok (low bound)", daemon_parse_cad_rssi_boot("-130", &v), 1);

    // Out of range / invalid.
    expect_int("parse -131 fails (too low)", daemon_parse_cad_rssi_boot("-131", &v), 0);
    expect_int("parse 1 fails (too high)", daemon_parse_cad_rssi_boot("1", &v), 0);
    expect_int("parse junk fails", daemon_parse_cad_rssi_boot("-90x", &v), 0);
    expect_int("parse empty fails", daemon_parse_cad_rssi_boot("", &v), 0);
    expect_int("parse null fails", daemon_parse_cad_rssi_boot(NULL, &v), 0);
}

static void test_default_unset(void)
{
    float v = 0.0f;
    daemon_cad_rssi_boot_reset();

    expect_int("default unset", daemon_cad_rssi_boot_effective(&v) ? 1 : 0, 0);
}

static void test_set_applies(void)
{
    float v = 0.0f;
    daemon_cad_rssi_boot_reset();

    expect_int("set ok", daemon_set_cad_rssi_boot_global("-85"), 1);
    expect_int("set effective", daemon_cad_rssi_boot_effective(&v) ? 1 : 0, 1);
    expect_int("set value", (int)v, -85);
}

static void test_invalid_setter_keeps_unset(void)
{
    float v = 0.0f;
    daemon_cad_rssi_boot_reset();

    expect_int("invalid setter fails", daemon_set_cad_rssi_boot_global("-131"), 0);
    expect_int("invalid leaves unset", daemon_cad_rssi_boot_effective(&v) ? 1 : 0, 0);
}

int main(void)
{
    test_parse_values();
    test_default_unset();
    test_set_applies();
    test_invalid_setter_keeps_unset();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
