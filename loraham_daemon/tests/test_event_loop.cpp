#include "../event_loop.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/*
 * Event-loop behavior tests.
 *
 * These tests exercise the public event_loop_* wrapper only. They intentionally
 * avoid backend names and backend-specific state so that the implementation can
 * change without rewriting the behavior contract.
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

static void test_init_reset_close(void)
{
    EventLoopSet set;

    expect_int("init succeeds", event_loop_init(&set), 0);
    expect_int("init starts empty", event_loop_has_registered_fds(&set), 0);

    event_loop_reset(&set);
    expect_int("reset clears registered fds",
               event_loop_has_registered_fds(&set), 0);

    event_loop_close(&set);
    expect_int("close clears registered fds",
               event_loop_has_registered_fds(&set), 0);

    event_loop_close(&set);
    expect_int("close is idempotent",
               event_loop_has_registered_fds(&set), 0);
}

static void test_wait_timeout_without_fds(void)
{
    EventLoopSet set;
    EventLoopReadySet ready;

    if (event_loop_init(&set) != 0) {
        g_fail++;
        printf("[FAIL] init for timeout\n");
        return;
    }

    expect_int("wait timeout with no fds",
               event_loop_wait(&set, &ready, 1000), 0);

    event_loop_close(&set);
}

static void test_wait_readable_pipe(void)
{
    EventLoopSet set;
    EventLoopReadySet ready;
    int fds[2];
    char ch = 'x';

    if (pipe(fds) != 0) {
        g_fail++;
        printf("[FAIL] pipe setup\n");
        return;
    }

    if (event_loop_init(&set) != 0) {
        close(fds[0]);
        close(fds[1]);
        g_fail++;
        printf("[FAIL] init for readable pipe\n");
        return;
    }

    event_loop_add_fd(&set, fds[0]);
    expect_int("fd registered", event_loop_has_registered_fds(&set), 1);

    (void)write(fds[1], &ch, 1);

    expect_int("wait returns readable",
               event_loop_wait(&set, &ready, 100000), 1);
    expect_int("ready helper sees fd",
               event_loop_ready_fd(&ready, fds[0]), 1);
    expect_int("read-ready helper sees fd",
               event_loop_ready_fd_read(&ready, fds[0]), 1);
    expect_int("ready helper ignores negative fd",
               event_loop_ready_fd(&ready, -1), 0);

    event_loop_close(&set);
    close(fds[0]);
    close(fds[1]);
}

static void test_wait_writable_pipe(void)
{
    EventLoopSet set;
    EventLoopReadySet ready;
    int fds[2];

    if (pipe(fds) != 0) {
        g_fail++;
        printf("[FAIL] writable pipe setup\n");
        return;
    }

    if (event_loop_init(&set) != 0) {
        close(fds[0]);
        close(fds[1]);
        g_fail++;
        printf("[FAIL] init for writable pipe\n");
        return;
    }

    event_loop_add_fd_events(&set, fds[1], EVENT_LOOP_EVENT_WRITE);
    expect_int("write fd registered", event_loop_has_registered_fds(&set), 1);

    expect_int("wait returns writable",
               event_loop_wait(&set, &ready, 100000), 1);
    expect_int("write-ready helper sees fd",
               event_loop_ready_fd_write(&ready, fds[1]), 1);

    event_loop_close(&set);
    close(fds[0]);
    close(fds[1]);
}


static void test_reset_keeps_loop_reusable(void)
{
    EventLoopSet set;
    EventLoopReadySet ready;
    int fds[2];
    char ch = 'x';

    if (pipe(fds) != 0) {
        g_fail++;
        printf("[FAIL] reset reuse pipe setup\n");
        return;
    }

    if (event_loop_init(&set) != 0) {
        close(fds[0]);
        close(fds[1]);
        g_fail++;
        printf("[FAIL] init for reset reuse\n");
        return;
    }

    event_loop_add_fd(&set, fds[0]);
    expect_int("registered before reuse reset",
               event_loop_has_registered_fds(&set), 1);

    event_loop_reset(&set);
    expect_int("reuse reset clears fds",
               event_loop_has_registered_fds(&set), 0);

    event_loop_add_fd(&set, fds[0]);
    expect_int("register after reset",
               event_loop_has_registered_fds(&set), 1);

    (void)write(fds[1], &ch, 1);

    expect_int("wait works after reset",
               event_loop_wait(&set, &ready, 100000), 1);
    expect_int("ready helper works after reset",
               event_loop_ready_fd_read(&ready, fds[0]), 1);

    event_loop_close(&set);
    close(fds[0]);
    close(fds[1]);
}

static void test_reset_clears_registered_fds(void)
{
    EventLoopSet set;
    int fds[2];

    if (pipe(fds) != 0) {
        g_fail++;
        printf("[FAIL] reset pipe setup\n");
        return;
    }

    if (event_loop_init(&set) != 0) {
        close(fds[0]);
        close(fds[1]);
        g_fail++;
        printf("[FAIL] init for reset\n");
        return;
    }

    event_loop_add_fd(&set, fds[0]);
    expect_int("registered before reset",
               event_loop_has_registered_fds(&set), 1);

    event_loop_reset(&set);
    expect_int("reset clears fds",
               event_loop_has_registered_fds(&set), 0);

    event_loop_close(&set);
    close(fds[0]);
    close(fds[1]);
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

    test_init_reset_close();
    test_wait_timeout_without_fds();
    test_wait_readable_pipe();
    test_wait_writable_pipe();
    test_reset_keeps_loop_reusable();
    test_reset_clears_registered_fds();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
