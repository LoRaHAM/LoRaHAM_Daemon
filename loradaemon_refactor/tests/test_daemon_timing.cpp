#include "../daemon_timing.h"

#include <stdio.h>
#include <string.h>

/*
 * Daemon timing unit tests.
 *
 * These tests lock the current counter-based tick behavior before deadline
 * timers are introduced.
 */

static int g_ok = 0;
static int g_fail = 0;

/* --- Test helpers --- */

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

/* --- Counter tick behavior --- */

static void test_plain_counter_tick(void)
{
    int counter = 0;

    expect_int("plain tick 1 not due", daemon_tick_due(&counter, 3), 0);
    expect_int("plain tick 1 counter", counter, 1);

    expect_int("plain tick 2 not due", daemon_tick_due(&counter, 3), 0);
    expect_int("plain tick 2 counter", counter, 2);

    expect_int("plain tick 3 due", daemon_tick_due(&counter, 3), 1);
    expect_int("plain tick reset", counter, 0);
}

static void test_state_counter_tick(void)
{
    DaemonTick tick;

    daemon_tick_init(&tick, 2);

    expect_int("state init counter", tick.counter, 0);
    expect_int("state init interval", tick.interval, 2);

    expect_int("state tick 1 not due", daemon_tick_state_due(&tick), 0);
    expect_int("state tick 1 counter", tick.counter, 1);

    expect_int("state tick 2 due", daemon_tick_state_due(&tick), 1);
    expect_int("state tick reset", tick.counter, 0);
}

/* --- CLI parsing and test sequence --- */

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

    test_plain_counter_tick();
    test_state_counter_tick();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
