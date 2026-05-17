#include "../event_loop.h"
#include "../event_loop_epoll.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/*
 * Event-loop unit tests.
 *
 * The daemon now uses epoll as its only event-loop backend.
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

/* --- epoll backend --- */

static void test_epoll_init_reset_close(void)
{
    EventLoopEpollSet set;

    expect_int("epoll init", event_loop_epoll_init(&set), 0);
    expect_int("epoll starts empty",
               event_loop_epoll_has_registered_fds(&set), 0);

    event_loop_epoll_reset(&set);
    expect_int("epoll reset keeps backend usable", set.epoll_fd >= 0, 1);
    expect_int("epoll reset clears fds",
               event_loop_epoll_has_registered_fds(&set), 0);

    event_loop_epoll_close(&set);
    expect_int("epoll close clears fd", set.epoll_fd, -1);
}

static void test_epoll_wait_readable_pipe(void)
{
    EventLoopEpollSet set;
    EventLoopEpollReadySet ready;
    int fds[2];
    char ch = 'x';

    if (pipe(fds) != 0) {
        g_fail++;
        printf("[FAIL] epoll pipe setup\n");
        return;
    }

    if (event_loop_epoll_init(&set) != 0) {
        close(fds[0]);
        close(fds[1]);
        g_fail++;
        printf("[FAIL] epoll init for pipe\n");
        return;
    }

    expect_int("epoll add pipe fd", event_loop_epoll_add_fd(&set, fds[0]), 0);
    expect_int("epoll has registered fds",
               event_loop_epoll_has_registered_fds(&set), 1);

    (void)write(fds[1], &ch, 1);

    expect_int("epoll wait returns readable",
               event_loop_epoll_wait(&set, &ready, 100000), 1);
    expect_int("epoll ready helper sees fd",
               event_loop_epoll_ready_fd(&ready, fds[0]), 1);
    expect_int("epoll ready helper ignores negative fd",
               event_loop_epoll_ready_fd(&ready, -1), 0);

    event_loop_epoll_close(&set);
    close(fds[0]);
    close(fds[1]);
}

static void test_epoll_wait_timeout(void)
{
    EventLoopEpollSet set;
    EventLoopEpollReadySet ready;

    if (event_loop_epoll_init(&set) != 0) {
        g_fail++;
        printf("[FAIL] epoll init for timeout\n");
        return;
    }

    expect_int("epoll wait timeout with no fds",
               event_loop_epoll_wait(&set, &ready, 1000), 0);

    event_loop_epoll_close(&set);
}

/* --- backend-neutral epoll wrapper --- */

static void test_backend_names(void)
{
    expect_int("backend name epoll",
               strcmp(event_loop_backend_name(EVENT_LOOP_BACKEND_EPOLL), "epoll") == 0, 1);
}

static void test_event_loop_init(void)
{
    EventLoopSet set;
    int ret;

    ret = event_loop_init(&set);

    expect_int("event-loop init uses epoll", ret, 0);
    expect_int("event-loop init backend epoll",
               event_loop_backend(&set) == EVENT_LOOP_BACKEND_EPOLL, 1);
    expect_int("event-loop init backend name epoll",
               strcmp(event_loop_backend_name(event_loop_backend(&set)), "epoll") == 0, 1);
    expect_int("event-loop init starts empty",
               event_loop_has_registered_fds(&set), 0);

    event_loop_close(&set);
}

static void test_backend_reset_preserves_epoll(void)
{
    EventLoopSet set;
    int fds[2];

    if (pipe(fds) != 0) {
        g_fail++;
        printf("[FAIL] reset epoll pipe setup\n");
        return;
    }

    if (event_loop_init(&set) != 0) {
        close(fds[0]);
        close(fds[1]);
        g_fail++;
        printf("[FAIL] reset event-loop init\n");
        return;
    }

    event_loop_add_fd(&set, fds[0]);
    expect_int("reset epoll has registered fds before reset",
               event_loop_has_registered_fds(&set), 1);

    event_loop_reset(&set);

    expect_int("reset preserves epoll backend",
               event_loop_backend(&set) == EVENT_LOOP_BACKEND_EPOLL, 1);
    expect_int("reset epoll clears registered fds",
               event_loop_has_registered_fds(&set), 0);

    event_loop_close(&set);
    close(fds[0]);
    close(fds[1]);
}

static void test_backend_close_epoll_clears_state(void)
{
    EventLoopSet set;
    int fds[2];

    if (pipe(fds) != 0) {
        g_fail++;
        printf("[FAIL] close epoll pipe setup\n");
        return;
    }

    if (event_loop_init(&set) != 0) {
        close(fds[0]);
        close(fds[1]);
        g_fail++;
        printf("[FAIL] close event-loop init\n");
        return;
    }

    event_loop_add_fd(&set, fds[0]);
    expect_int("close epoll has registered fds before close",
               event_loop_has_registered_fds(&set), 1);

    event_loop_close(&set);

    expect_int("close epoll keeps epoll backend",
               event_loop_backend(&set) == EVENT_LOOP_BACKEND_EPOLL, 1);
    expect_int("close epoll clears registered fds",
               event_loop_has_registered_fds(&set), 0);

    event_loop_close(&set);
    expect_int("close epoll is idempotent",
               event_loop_backend(&set) == EVENT_LOOP_BACKEND_EPOLL, 1);

    close(fds[0]);
    close(fds[1]);
}

static void test_backend_neutral_aliases(void)
{
    EventLoopSet set;
    int fds[2];

    if (pipe(fds) != 0) {
        g_fail++;
        printf("[FAIL] generic alias pipe setup\n");
        return;
    }

    if (event_loop_init(&set) != 0) {
        close(fds[0]);
        close(fds[1]);
        g_fail++;
        printf("[FAIL] generic alias event-loop init\n");
        return;
    }

    expect_int("generic starts empty",
               event_loop_has_registered_fds(&set), 0);

    event_loop_add_fd(&set, fds[0]);
    expect_int("generic has registered fds",
               event_loop_has_registered_fds(&set), 1);

    event_loop_reset(&set);
    expect_int("generic reset clears registered fds",
               event_loop_has_registered_fds(&set), 0);

    event_loop_close(&set);
    close(fds[0]);
    close(fds[1]);
}

static void test_backend_neutral_wait_readable_pipe(void)
{
    EventLoopSet set;
    EventLoopReadySet ready;
    int fds[2];
    char ch = 'x';

    if (pipe(fds) != 0) {
        g_fail++;
        printf("[FAIL] generic pipe setup\n");
        return;
    }

    if (event_loop_init(&set) != 0) {
        close(fds[0]);
        close(fds[1]);
        g_fail++;
        printf("[FAIL] generic event-loop init\n");
        return;
    }

    event_loop_add_fd(&set, fds[0]);
    (void)write(fds[1], &ch, 1);

    expect_int("generic wait returns readable",
               event_loop_wait(&set, &ready, 100000), 1);
    expect_int("generic ready helper sees fd",
               event_loop_ready_fd(&ready, fds[0]), 1);

    event_loop_close(&set);
    close(fds[0]);
    close(fds[1]);
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

    test_epoll_init_reset_close();
    test_epoll_wait_readable_pipe();
    test_epoll_wait_timeout();
    test_backend_names();
    test_event_loop_init();
    test_backend_reset_preserves_epoll();
    test_backend_close_epoll_clears_state();
    test_backend_neutral_aliases();
    test_backend_neutral_wait_readable_pipe();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
