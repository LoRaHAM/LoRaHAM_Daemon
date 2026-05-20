#include "../daemon_lifecycle.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>

/*
 * Daemon lifecycle tests.
 *
 * These lock stop-request behavior before signal handling is wired into the
 * daemon main loop.
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

/* --- Stop request behavior --- */

static void test_stop_flag_reset_and_request(void)
{
    daemon_lifecycle_reset_stop();
    expect_int("stop initially clear", daemon_lifecycle_stop_requested(), 0);

    daemon_lifecycle_request_stop(SIGTERM);
    expect_int("stop requested after handler", daemon_lifecycle_stop_requested(), 1);

    daemon_lifecycle_reset_stop();
    expect_int("stop clear after reset", daemon_lifecycle_stop_requested(), 0);
}

static void test_stop_request_is_idempotent(void)
{
    daemon_lifecycle_reset_stop();

    daemon_lifecycle_request_stop(SIGINT);
    daemon_lifecycle_request_stop(SIGTERM);

    expect_int("stop request idempotent", daemon_lifecycle_stop_requested(), 1);
}

static void test_signal_install_and_raise(void)
{
    daemon_lifecycle_reset_stop();

    expect_int("signal handler install",
               daemon_lifecycle_install_signal_handlers(), 0);

    raise(SIGTERM);

    expect_int("SIGTERM requests stop", daemon_lifecycle_stop_requested(), 1);

    daemon_lifecycle_reset_stop();
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

    test_stop_flag_reset_and_request();
    test_stop_request_is_idempotent();
    test_signal_install_and_raise();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
