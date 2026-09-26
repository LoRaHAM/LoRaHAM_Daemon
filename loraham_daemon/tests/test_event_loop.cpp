#include "../event_loop.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
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

/* A one-byte poke to wake the loop, or the byte drained afterwards. GCC's warn_unused_result
   on write()/read() is NOT silenced by a (void) cast, so the result is consumed here. The
   effect is proved by the assertion that follows each call, not by the return value. */
#define POKE(expr) do { ssize_t poke_rc_ = (expr); (void)poke_rc_; } while (0)

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

    POKE(write(fds[1], &ch, 1));

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

    POKE(write(fds[1], &ch, 1));

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

static void test_registration_capacity(void)
{
    enum { TEST_FDS = 65 };
    EventLoopSet set;
    EventLoopReadySet ready;
    int pipes[TEST_FDS][2];
    int created = 0;
    char ch = 'x';

    if (event_loop_init(&set) != 0) {
        g_fail++;
        printf("[FAIL] init for registration capacity\n");
        return;
    }

    for (int i = 0; i < TEST_FDS; i++) {
        if (pipe(pipes[i]) != 0) {
            g_fail++;
            printf("[FAIL] capacity pipe setup\n");
            break;
        }

        created++;
        event_loop_add_fd(&set, pipes[i][0]);
    }

    if (created == TEST_FDS) {
        expect_int("registration remains clear above old limit",
                   event_loop_registration_failed(&set), 0);

        POKE(write(pipes[TEST_FDS - 1][1], &ch, 1));

        expect_int("65th registered fd is readable",
                   event_loop_wait(&set, &ready, 100000), 1);
        expect_int("ready helper sees 65th fd",
                   event_loop_ready_fd_read(&ready,
                                            pipes[TEST_FDS - 1][0]), 1);
    }

    event_loop_close(&set);

    for (int i = 0; i < created; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
}

static void test_registration_failure_is_not_silent(void)
{
    EventLoopSet set;
    EventLoopReadySet ready;

    if (event_loop_init(&set) != 0) {
        g_fail++;
        printf("[FAIL] init for registration failure\n");
        return;
    }

    errno = 0;
    event_loop_add_fd(&set, -1);

    expect_int("invalid registration is recorded",
               event_loop_registration_failed(&set), 1);
    expect_int("invalid registration errno", errno, EINVAL);
    expect_int("wait fails after registration failure",
               event_loop_wait(&set, &ready, 1000), -1);

    event_loop_reset(&set);

    expect_int("reset clears registration failure",
               event_loop_registration_failed(&set), 0);
    expect_int("wait works after registration reset",
               event_loop_wait(&set, &ready, 1000), 0);

    event_loop_close(&set);
}

/* Audit L2: the daemon's fd-sync path checks the registration-failure flag
 * BEFORE reconcile_begin — a failure early-return must never leave a dangling
 * reconcile epoch that would EBUSY the next begin. */
static void test_failed_registration_skips_reconcile_epoch(void)
{
    EventLoopSet set;

    if (event_loop_init(&set) != 0) {
        g_fail++;
        printf("[FAIL] init for reconcile-epoch test\n");
        return;
    }

    errno = 0;
    event_loop_add_fd(&set, -1);
    expect_int("epoch: failure flag pre-set",
               event_loop_registration_failed(&set), 1);

    /* The daemon sync ordering: flag checked first, begin skipped. */
    if (!event_loop_registration_failed(&set))
        event_loop_reconcile_begin(&set);
    expect_int("epoch: no dangling reconcile epoch",
               set.epoll_backend.reconcile_active, 0);

    /* After recovery a begin must not see EBUSY from a stale epoch. */
    event_loop_reset(&set);
    errno = 0;
    event_loop_reconcile_begin(&set);
    expect_int("epoch: begin after reset succeeds",
               event_loop_registration_failed(&set), 0);
    expect_int("epoch: reconcile active after begin",
               set.epoll_backend.reconcile_active, 1);
    event_loop_reconcile_end(&set);

    event_loop_close(&set);
}

static void test_reconcile_keeps_read_watch(void)
{
    EventLoopSet set;
    EventLoopReadySet ready;
    int fds[2];
    const char owner = 'r';
    char ch = 'x';
    char received;

    if (pipe(fds) != 0) {
        g_fail++;
        printf("[FAIL] reconcile read pipe setup\n");
        return;
    }

    if (event_loop_init(&set) != 0) {
        close(fds[0]);
        close(fds[1]);
        g_fail++;
        printf("[FAIL] reconcile read init\n");
        return;
    }

    event_loop_reconcile_begin(&set);
    event_loop_reconcile_fd(&set, &owner, 1u, fds[0],
                            EVENT_LOOP_EVENT_READ);
    event_loop_reconcile_end(&set);

    POKE(write(fds[1], &ch, 1));
    expect_int("reconcile first read wait",
               event_loop_wait(&set, &ready, 100000), 1);
    expect_int("reconcile first read ready",
               event_loop_ready_fd_read(&ready, fds[0]), 1);
    POKE(read(fds[0], &received, 1));

    event_loop_reconcile_begin(&set);
    event_loop_reconcile_fd(&set, &owner, 1u, fds[0],
                            EVENT_LOOP_EVENT_READ);
    event_loop_reconcile_end(&set);

    POKE(write(fds[1], &ch, 1));
    expect_int("reconcile repeated read wait",
               event_loop_wait(&set, &ready, 100000), 1);
    expect_int("reconcile repeated read ready",
               event_loop_ready_fd_read(&ready, fds[0]), 1);

    event_loop_close(&set);
    close(fds[0]);
    close(fds[1]);
}

static void test_reconcile_updates_write_interest(void)
{
    EventLoopSet set;
    EventLoopReadySet ready;
    int fds[2];
    const char owner = 'w';

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        g_fail++;
        printf("[FAIL] reconcile write socketpair\n");
        return;
    }

    if (event_loop_init(&set) != 0) {
        close(fds[0]);
        close(fds[1]);
        g_fail++;
        printf("[FAIL] reconcile write init\n");
        return;
    }

    event_loop_reconcile_begin(&set);
    event_loop_reconcile_fd(&set, &owner, 1u, fds[0],
                            EVENT_LOOP_EVENT_WRITE);
    event_loop_reconcile_end(&set);

    expect_int("reconcile write ready",
               event_loop_wait(&set, &ready, 100000), 1);
    expect_int("reconcile write event",
               event_loop_ready_fd_write(&ready, fds[0]), 1);

    event_loop_reconcile_begin(&set);
    event_loop_reconcile_fd(&set, &owner, 1u, fds[0],
                            EVENT_LOOP_EVENT_READ);
    event_loop_reconcile_end(&set);

    expect_int("reconcile write removed",
               event_loop_wait(&set, &ready, 1000), 0);

    event_loop_close(&set);
    close(fds[0]);
    close(fds[1]);
}

static void test_reconcile_removes_stale_watch(void)
{
    EventLoopSet set;
    EventLoopReadySet ready;
    int fds[2];
    const char owner = 's';
    char ch = 'x';

    if (pipe(fds) != 0) {
        g_fail++;
        printf("[FAIL] reconcile stale pipe setup\n");
        return;
    }

    if (event_loop_init(&set) != 0) {
        close(fds[0]);
        close(fds[1]);
        g_fail++;
        printf("[FAIL] reconcile stale init\n");
        return;
    }

    event_loop_reconcile_begin(&set);
    event_loop_reconcile_fd(&set, &owner, 1u, fds[0],
                            EVENT_LOOP_EVENT_READ);
    event_loop_reconcile_end(&set);

    event_loop_reconcile_begin(&set);
    event_loop_reconcile_end(&set);

    expect_int("reconcile stale removed",
               event_loop_has_registered_fds(&set), 0);

    POKE(write(fds[1], &ch, 1));
    expect_int("reconcile stale wait timeout",
               event_loop_wait(&set, &ready, 1000), 0);

    event_loop_close(&set);
    close(fds[0]);
    close(fds[1]);
}

static void test_reconcile_reuses_closed_fd(void)
{
    EventLoopSet set;
    EventLoopReadySet ready;
    int first[2];
    int replacement[2];
    const char owner = 'u';
    char ch = 'x';
    int old_fd;

    if (pipe(first) != 0) {
        g_fail++;
        printf("[FAIL] reconcile reuse first pipe\n");
        return;
    }

    if (event_loop_init(&set) != 0) {
        close(first[0]);
        close(first[1]);
        g_fail++;
        printf("[FAIL] reconcile reuse init\n");
        return;
    }

    event_loop_reconcile_begin(&set);
    event_loop_reconcile_fd(&set, &owner, 1u, first[0],
                            EVENT_LOOP_EVENT_READ);
    event_loop_reconcile_end(&set);

    old_fd = first[0];
    close(first[0]);

    if (pipe(replacement) != 0) {
        event_loop_close(&set);
        close(first[1]);
        g_fail++;
        printf("[FAIL] reconcile reuse replacement pipe\n");
        return;
    }

    if (replacement[0] != old_fd) {
        if (dup2(replacement[0], old_fd) < 0) {
            event_loop_close(&set);
            close(first[1]);
            close(replacement[0]);
            close(replacement[1]);
            g_fail++;
            printf("[FAIL] reconcile reuse dup2\n");
            return;
        }

        close(replacement[0]);
        replacement[0] = old_fd;
    }

    event_loop_reconcile_begin(&set);
    event_loop_reconcile_fd(&set, &owner, 2u, replacement[0],
                            EVENT_LOOP_EVENT_READ);
    event_loop_reconcile_end(&set);

    POKE(write(replacement[1], &ch, 1));
    expect_int("reconcile reused fd wait",
               event_loop_wait(&set, &ready, 100000), 1);
    expect_int("reconcile reused fd ready",
               event_loop_ready_fd_read(&ready, replacement[0]), 1);

    event_loop_close(&set);
    close(first[1]);
    close(replacement[0]);
    close(replacement[1]);
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
    test_registration_capacity();
    test_registration_failure_is_not_silent();
    test_failed_registration_skips_reconcile_epoch();
    test_reconcile_keeps_read_watch();
    test_reconcile_updates_write_interest();
    test_reconcile_removes_stale_watch();
    test_reconcile_reuses_closed_fd();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
