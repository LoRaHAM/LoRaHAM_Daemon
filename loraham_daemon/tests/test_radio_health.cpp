#include "../radio_health.h"

#include <stdio.h>
#include <string.h>

/* --- Radio health helper tests --- */

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

static void expect_str(const char *name, const char *actual, const char *expected)
{
    if (strcmp(actual, expected) == 0) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: expected %s, got %s\n", name, expected, actual);
    }
}

static void test_radio_health_names(void)
{
    expect_str("health uninitialized name",
               radio_health_name(RADIO_HEALTH_UNINITIALIZED),
               "UNINITIALIZED");
    expect_str("health ready name",
               radio_health_name(RADIO_HEALTH_READY),
               "READY");
    expect_str("health failed name",
               radio_health_name(RADIO_HEALTH_FAILED),
               "FAILED");
}

static void test_radio_health_ready(void)
{
    expect_int("uninitialized is not ready",
               radio_health_is_ready(RADIO_HEALTH_UNINITIALIZED), 0);
    expect_int("ready is ready",
               radio_health_is_ready(RADIO_HEALTH_READY), 1);
    expect_int("failed is not ready",
               radio_health_is_ready(RADIO_HEALTH_FAILED), 0);
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

    test_radio_health_names();
    test_radio_health_ready();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
