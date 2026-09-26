#include "../daemon_tx_policy.h"

#include <stdio.h>
#include <string.h>

/* --- CAD/TX policy tests ------------------------------------------------- */

static int g_ok = 0;
static int g_fail = 0;

static void expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual == expected) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: expected %u, got %u\n", name, expected, actual);
    }
}

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

static void test_policy_constants(void)
{
    expect_u32("busy timeout ms", DAEMON_TX_POLICY_BUSY_TIMEOUT_MS, 120000u);
    expect_u32("cad wait timeout ms", DAEMON_TX_POLICY_CAD_WAIT_TIMEOUT_MS, 1500u);
    expect_u32("cad idle stable ms", DAEMON_TX_POLICY_CAD_IDLE_STABLE_MS, 250u);
    expect_u32("poll interval ms", DAEMON_TX_POLICY_POLL_INTERVAL_MS, 50u);
    expect_u32("poll interval usec",
               daemon_tx_policy_poll_interval_usec(),
               50000u);
    expect_int("send after cad timeout",
               daemon_tx_policy_send_after_cad_timeout(),
               0);
}

static void test_tick_rounding(void)
{
    expect_u32("ticks exact",
               daemon_tx_policy_ticks_for_ms(1500u, 50u),
               30u);
    expect_u32("ticks rounded up",
               daemon_tx_policy_ticks_for_ms(251u, 50u),
               6u);
    expect_u32("ticks zero interval safe",
               daemon_tx_policy_ticks_for_ms(500u, 0u),
               0u);
}

static void test_named_ticks(void)
{
    expect_u32("busy timeout ticks",
               daemon_tx_policy_busy_timeout_ticks(),
               2400u);
    expect_u32("cad wait ticks",
               daemon_tx_policy_cad_wait_ticks(),
               30u);
    expect_u32("cad stable ticks",
               daemon_tx_policy_cad_idle_stable_ticks(),
               5u);
}

static void test_timeout_reached(void)
{
    expect_int("timeout before",
               daemon_tx_policy_timeout_reached(1499u, 1500u),
               0);
    expect_int("timeout exactly",
               daemon_tx_policy_timeout_reached(1500u, 1500u),
               1);
    expect_int("timeout after",
               daemon_tx_policy_timeout_reached(1501u, 1500u),
               1);
    expect_int("timeout disabled",
               daemon_tx_policy_timeout_reached(1u, 0u),
               0);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--bin") == 0) {
            if (i + 1 >= argc) {
                printf("Usage: %s [--bin ignored]\n", argv[0]);
                return 2;
            }
            i++;
        } else if (strcmp(argv[i], "--help") == 0 ||
                   strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [--bin ignored]\n", argv[0]);
            return 0;
        } else {
            printf("Usage: %s [--bin ignored]\n", argv[0]);
            return 2;
        }
    }

    test_policy_constants();
    test_tick_rounding();
    test_named_ticks();
    test_timeout_reached();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
