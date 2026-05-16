#include "../event_loop.h"

#include <stdio.h>
#include <string.h>

/*
 * Event-loop unit tests.
 *
 * This locks the select() fd-set wrapper before the daemon loop is moved
 * behind the event-loop boundary.
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

/* --- select() fd-set wrapper --- */

static void test_select_set_reset(void)
{
    EventLoopSelectSet set;

    event_loop_select_reset(&set);

    expect_int("reset maxfd", set.maxfd, 0);
    expect_int("reset missing fd", event_loop_select_has_fd(&set, 3), 0);
}

static void test_select_set_add_fd(void)
{
    EventLoopSelectSet set;

    event_loop_select_reset(&set);
    event_loop_select_add_fd(&set, 3);
    event_loop_select_add_fd(&set, 7);

    expect_int("fd 3 present", event_loop_select_has_fd(&set, 3), 1);
    expect_int("fd 7 present", event_loop_select_has_fd(&set, 7), 1);
    expect_int("fd 4 missing", event_loop_select_has_fd(&set, 4), 0);
    expect_int("maxfd tracks highest plus one", set.maxfd, 8);
}

static void test_select_set_ignores_negative_fd(void)
{
    EventLoopSelectSet set;

    event_loop_select_reset(&set);
    event_loop_select_add_fd(&set, -1);

    expect_int("negative fd ignored", set.maxfd, 0);
    expect_int("negative fd missing", event_loop_select_has_fd(&set, -1), 0);
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

    test_select_set_reset();
    test_select_set_add_fd();
    test_select_set_ignores_negative_fd();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
