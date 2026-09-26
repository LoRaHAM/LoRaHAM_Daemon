#include "../daemon_high_power_boot.h"

#include <stdio.h>

/* --- Boot high-power permission tests ------------------------------------ */
/*
 * The flag is a bare, process-lifetime permission: unset means OFF, the setter
 * turns it on, nothing but the test reset turns it off again. There is no
 * string to parse, so there is no invalid-value case here -- an argument to
 * `--high-power` is refused by getopt itself (see test_interface_baseline).
 */

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

static void test_default_is_off(void)
{
    daemon_high_power_boot_reset();
    expect_int("unset -> OFF", daemon_high_power_enabled(), 0);
}

static void test_set_turns_on(void)
{
    daemon_high_power_boot_reset();
    daemon_set_high_power_boot_global();
    expect_int("set -> ON", daemon_high_power_enabled(), 1);
}

static void test_set_is_idempotent(void)
{
    daemon_high_power_boot_reset();
    daemon_set_high_power_boot_global();
    daemon_set_high_power_boot_global();
    expect_int("set twice -> still ON", daemon_high_power_enabled(), 1);
}

static void test_reset_turns_off(void)
{
    daemon_set_high_power_boot_global();
    daemon_high_power_boot_reset();
    expect_int("reset -> OFF", daemon_high_power_enabled(), 0);
}

int main(void)
{
    test_default_is_off();
    test_set_turns_on();
    test_set_is_idempotent();
    test_reset_turns_off();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
