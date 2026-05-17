#include "../event_loop.h"
#include "../event_loop_epoll.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

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

    expect_int("reset fd limit", event_loop_select_fd_limit(&set), 0);
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
    expect_int("fd limit tracks highest plus one", event_loop_select_fd_limit(&set), 8);
}

static void test_select_set_ignores_negative_fd(void)
{
    EventLoopSelectSet set;

    event_loop_select_reset(&set);
    event_loop_select_add_fd(&set, -1);

    expect_int("negative fd ignored", event_loop_select_fd_limit(&set), 0);
    expect_int("negative fd missing", event_loop_select_has_fd(&set, -1), 0);
}


static void test_select_wait_readable_pipe(void)
{
    EventLoopSelectSet set;
    EventLoopSelectReadySet ready;
    int fds[2];
    char ch = 'x';

    if (pipe(fds) != 0) {
        g_fail++;
        printf("[FAIL] pipe setup\n");
        return;
    }

    event_loop_select_reset(&set);
    event_loop_select_add_fd(&set, fds[0]);

    if (write(fds[1], &ch, 1) != 1) {
        close(fds[0]);
        close(fds[1]);
        g_fail++;
        printf("[FAIL] pipe write\n");
        return;
    }

    expect_int("select wait returns readable",
               event_loop_select_wait(&set, &ready, 100000), 1);
    expect_int("select wait marks pipe readable",
               event_loop_select_ready_fd(&ready, fds[0]), 1);

    close(fds[0]);
    close(fds[1]);
}

static void test_select_wait_timeout(void)
{
    EventLoopSelectSet set;
    EventLoopSelectReadySet ready;

    event_loop_select_reset(&set);

    expect_int("select wait timeout with no fds",
               event_loop_select_wait(&set, &ready, 1000), 0);
}


static void test_select_ready_fd(void)
{
    EventLoopSelectSet set;
    EventLoopSelectReadySet ready;
    int fds[2];
    char ch = 'x';

    if (pipe(fds) != 0) {
        g_fail++;
        printf("[FAIL] pipe setup for ready fd\n");
        return;
    }

    event_loop_select_reset(&set);
    event_loop_select_add_fd(&set, fds[0]);
    (void)write(fds[1], &ch, 1);

    expect_int("ready wait returns one fd",
               event_loop_select_wait(&set, &ready, 100000), 1);
    expect_int("ready helper sees fd",
               event_loop_select_ready_fd(&ready, fds[0]), 1);
    expect_int("ready helper ignores negative fd",
               event_loop_select_ready_fd(&ready, -1), 0);

    close(fds[0]);
    close(fds[1]);
}



/* --- epoll backend scaffold --- */

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

/* --- backend-neutral wrapper --- */

static void test_backend_selection(void)
{
    EventLoopSet set;

    event_loop_init_select(&set);

    expect_int("generic backend select",
               event_loop_backend(&set) == EVENT_LOOP_BACKEND_SELECT, 1);
    expect_int("generic init clears registered fds",
               event_loop_has_registered_fds(&set), 0);
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

    if (event_loop_init_epoll(&set) != 0) {
        close(fds[0]);
        close(fds[1]);
        g_fail++;
        printf("[FAIL] reset epoll init\n");
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

static void test_backend_reset_preserves_select(void)
{
    EventLoopSet set;

    event_loop_init_select(&set);
    event_loop_add_fd(&set, 5);
    expect_int("reset select has registered fds before reset",
               event_loop_has_registered_fds(&set), 1);

    event_loop_reset(&set);

    expect_int("reset preserves select backend",
               event_loop_backend(&set) == EVENT_LOOP_BACKEND_SELECT, 1);
    expect_int("reset select clears registered fds",
               event_loop_has_registered_fds(&set), 0);
}
static void test_backend_neutral_aliases(void)
{
    EventLoopSet set;

    event_loop_reset(&set);
    event_loop_add_fd(&set, 5);

    expect_int("generic fd present", event_loop_has_fd(&set, 5), 1);
    expect_int("generic fd missing", event_loop_has_fd(&set, 6), 0);
    expect_int("generic has registered fds", event_loop_has_registered_fds(&set), 1);

    event_loop_reset(&set);
    expect_int("generic reset clears registered fds",
               event_loop_has_registered_fds(&set), 0);
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

    event_loop_reset(&set);
    event_loop_add_fd(&set, fds[0]);
    (void)write(fds[1], &ch, 1);

    expect_int("generic wait returns readable",
               event_loop_wait(&set, &ready, 100000), 1);
    expect_int("generic ready helper sees fd",
               event_loop_ready_fd(&ready, fds[0]), 1);

    close(fds[0]);
    close(fds[1]);
}


static void test_backend_neutral_epoll_wait_readable_pipe(void)
{
    EventLoopSet set;
    EventLoopReadySet ready;
    int fds[2];
    char ch = 'x';

    if (pipe(fds) != 0) {
        g_fail++;
        printf("[FAIL] generic epoll pipe setup\n");
        return;
    }

    if (event_loop_init_epoll(&set) != 0) {
        close(fds[0]);
        close(fds[1]);
        g_fail++;
        printf("[FAIL] generic epoll init\n");
        return;
    }

    expect_int("generic backend epoll",
               event_loop_backend(&set) == EVENT_LOOP_BACKEND_EPOLL, 1);

    event_loop_add_fd(&set, fds[0]);
    expect_int("generic epoll has registered fds",
               event_loop_has_registered_fds(&set), 1);

    (void)write(fds[1], &ch, 1);

    expect_int("generic epoll wait returns readable",
               event_loop_wait(&set, &ready, 100000), 1);
    expect_int("generic epoll ready helper sees fd",
               event_loop_ready_fd(&ready, fds[0]), 1);

    event_loop_close(&set);
    expect_int("generic epoll close switches to select",
               event_loop_backend(&set) == EVENT_LOOP_BACKEND_SELECT, 1);

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

    test_select_set_reset();
    test_select_set_add_fd();
    test_select_set_ignores_negative_fd();
    test_select_wait_readable_pipe();
    test_select_wait_timeout();
    test_select_ready_fd();
    test_epoll_init_reset_close();
    test_epoll_wait_readable_pipe();
    test_epoll_wait_timeout();
    test_backend_selection();
    test_backend_reset_preserves_select();
    test_backend_reset_preserves_epoll();
    test_backend_neutral_aliases();
    test_backend_neutral_wait_readable_pipe();
    test_backend_neutral_epoll_wait_readable_pipe();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
