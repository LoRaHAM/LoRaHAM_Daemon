/*
 * Unit tests for LockingPiHal -- the process-shared SPI transaction lock.
 *
 * These tests construct a LockingPiHal (which does NOT open SPI/GPIO; only
 * init() would) and exercise spiBeginTransaction()/spiEndTransaction(), then
 * observe the shared lock file through independent file descriptors and a child
 * process.  No radio hardware is required.
 */

#include "../locking_pihal.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int g_ok = 0;
static int g_fail = 0;

static char g_tmpdir[256];

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

static const char *lock_path(void)
{
    static char path[320];

    snprintf(path, sizeof(path), "%s/spi0.lock", loraham_runtime_dir());
    return path;
}

/* Probe the shared lock file through a fresh, independent descriptor.
 * Returns 1 if the lock was free (and momentarily grabbed+released here),
 * 0 if it was held by someone else, -1 on error. */
static int probe_lock_free(void)
{
    int fd = open(lock_path(), O_CREAT | O_RDWR, 0660);
    int got;

    if (fd < 0)
        return -1;

    got = flock(fd, LOCK_EX | LOCK_NB);
    if (got == 0) {
        flock(fd, LOCK_UN);
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}

static void test_basic_lock_release(void)
{
    LockingPiHal hal(0);

    expect_int("free before begin", probe_lock_free(), 1);

    hal.spiBeginTransaction();
    expect_int("held during transaction", probe_lock_free(), 0);

    hal.spiEndTransaction();
    expect_int("free after end", probe_lock_free(), 1);
}

static void test_recursion_guard(void)
{
    LockingPiHal hal(0);

    hal.spiBeginTransaction();   /* depth 1 */
    hal.spiBeginTransaction();   /* depth 2 */
    expect_int("held at depth 2", probe_lock_free(), 0);

    hal.spiEndTransaction();     /* depth 1 -- must stay held */
    expect_int("still held at depth 1", probe_lock_free(), 0);

    hal.spiEndTransaction();     /* depth 0 -- now released */
    expect_int("released at depth 0", probe_lock_free(), 1);
}

static void test_two_instances_share_one_lock(void)
{
    LockingPiHal a(0);
    LockingPiHal b(0);

    a.spiBeginTransaction();
    /* An independent descriptor (the same view another instance/process has)
     * must see the lock held. */
    expect_int("instance A transaction excludes others", probe_lock_free(), 0);
    a.spiEndTransaction();

    b.spiBeginTransaction();
    expect_int("instance B transaction excludes others", probe_lock_free(), 0);
    b.spiEndTransaction();
    expect_int("free after both instances done", probe_lock_free(), 1);
}

/* The core guarantee: a *separate process* cannot enter an SPI transaction
 * while this process holds one. */
static void test_cross_process_exclusion(void)
{
    LockingPiHal hal(0);
    pid_t pid;
    int status;

    hal.spiBeginTransaction();

    pid = fork();
    if (pid == 0) {
        /* Child: own process, opens its own descriptor. Should see HELD. */
        _exit(probe_lock_free() == 0 ? 0 : 1);
    }
    waitpid(pid, &status, 0);
    expect_int("child process sees lock held",
               (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 1 : 0, 1);

    hal.spiEndTransaction();

    pid = fork();
    if (pid == 0) {
        /* Child should now see FREE. */
        _exit(probe_lock_free() == 1 ? 0 : 1);
    }
    waitpid(pid, &status, 0);
    expect_int("child process sees lock free after end",
               (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 1 : 0, 1);
}

/* --- fail-closed behavior ------------------------------------------------ */

/* When the trusted lock directory cannot be used, the HAL must report
 * not-ready (so the daemon refuses to start the radio) and must NOT silently
 * fall back to /tmp. */
static void test_fail_closed_when_dir_unusable(void)
{
    /* /proc is not writable: mkdir + open both fail, no fallback. */
    setenv("LORAHAM_RUNTIME_DIR", "/proc/loraham_nonexistent", 1);

    {
        LockingPiHal hal(0);
        expect_int("lock dir unusable -> not ready", hal.spi_lock_ready() ? 1 : 0, 0);
    }

    setenv("LORAHAM_RUNTIME_DIR", g_tmpdir, 1);
    {
        LockingPiHal hal(0);
        expect_int("trusted dir usable -> ready", hal.spi_lock_ready() ? 1 : 0, 1);
    }
}

/* No SPI transaction may begin without the lock: with no lock established,
 * spiBeginTransaction() must fail closed (controlled fatal), never proceed. */
static void test_no_transaction_without_lock(void)
{
    pid_t pid = fork();

    if (pid == 0) {
        setenv("LORAHAM_RUNTIME_DIR", "/proc/loraham_nonexistent", 1);
        LockingPiHal hal(0);          /* not lock-ready */
        hal.spiBeginTransaction();    /* must _exit(LORAHAM_EXIT_RUNTIME_RADIO_IO_ERROR) */
        _exit(0);                     /* reaching here = FAILED (proceeded) */
    }

    int status = 0;
    waitpid(pid, &status, 0);
    expect_int("spiBeginTransaction without lock is fatal",
               (WIFEXITED(status) && WEXITSTATUS(status) == LORAHAM_EXIT_RUNTIME_RADIO_IO_ERROR)
                   ? 1 : 0, 1);
}

/* --- flock acquire helper (EINTR retry vs hard failure) ------------------ */

static int g_fake_calls = 0;
static int g_fake_eintr_remaining = 0;

static int fake_flock_eintr(int fd, int op)
{
    (void)fd;
    (void)op;
    g_fake_calls++;
    if (g_fake_eintr_remaining-- > 0) {
        errno = EINTR;
        return -1;
    }
    return 0;
}

static int fake_flock_hardfail(int fd, int op)
{
    (void)fd;
    (void)op;
    g_fake_calls++;
    errno = EIO;
    return -1;
}

static void test_flock_eintr_retry(void)
{
    g_fake_calls = 0;
    g_fake_eintr_remaining = 2;   /* two EINTRs then success */

    int rc = loraham_flock_acquire_ex(3, fake_flock_eintr);

    expect_int("EINTR retried until success", rc, 0);
    expect_int("EINTR retried exactly 3 calls", g_fake_calls, 3);
}

static void test_flock_hard_failure(void)
{
    g_fake_calls = 0;

    int rc = loraham_flock_acquire_ex(3, fake_flock_hardfail);

    expect_int("hard flock failure reported (-1)", rc, -1);
    expect_int("hard flock failure not retried", g_fake_calls, 1);
}

/* --- deadline-bounded acquisition (audit P1-3) ---------------------------- */

static int g_busy_calls = 0;
static int g_busy_remaining = 0;

static int fake_flock_busy(int fd, int op)
{
    (void)fd;
    (void)op;
    g_busy_calls++;
    if (g_busy_remaining-- > 0) {
        errno = EWOULDBLOCK;
        return -1;
    }
    return 0;
}

static int fake_flock_busy_forever(int fd, int op)
{
    (void)fd;
    (void)op;
    g_busy_calls++;
    errno = EWOULDBLOCK;
    return -1;
}

static void test_flock_deadline_recovers(void)
{
    g_busy_calls = 0;
    g_busy_remaining = 3;   /* three busy polls, then the peer releases */

    int rc = loraham_flock_acquire_ex_deadline(3, fake_flock_busy, 2000L);

    expect_int("deadline: acquired after contention", rc, 0);
    expect_int("deadline: polled until free", g_busy_calls, 4);
}

static void test_flock_deadline_expires(void)
{
    g_busy_calls = 0;
    errno = 0;

    int rc = loraham_flock_acquire_ex_deadline(3, fake_flock_busy_forever, 20L);

    expect_int("deadline: wedged peer times out", rc, -1);
    expect_int("deadline: errno is ETIMEDOUT", errno == ETIMEDOUT ? 1 : 0, 1);
    expect_int("deadline: polled more than once", g_busy_calls > 1 ? 1 : 0, 1);
}

static void test_flock_deadline_hard_failure(void)
{
    g_fake_calls = 0;

    int rc = loraham_flock_acquire_ex_deadline(3, fake_flock_hardfail, 2000L);

    expect_int("deadline: hard failure reported (-1)", rc, -1);
    expect_int("deadline: hard failure not retried", g_fake_calls, 1);
    expect_int("deadline: hard failure keeps errno", errno == EIO ? 1 : 0, 1);
}

/* --- unlock (LOCK_UN) failure handling (M4-P0-3) ------------------------- */

static int g_unlock_calls = 0;
static int g_unlock_eintr_remaining = 0;

static int fake_flock_unlock_eintr(int fd, int op)
{
    (void)fd;
    if (op == LOCK_UN) {
        g_unlock_calls++;
        if (g_unlock_eintr_remaining-- > 0) {
            errno = EINTR;
            return -1;
        }
    }
    return 0;
}

/* LOCK_EX succeeds (so a transaction can begin), LOCK_UN fails hard. */
static int fake_flock_unlock_hardfail(int fd, int op)
{
    (void)fd;
    if (op == LOCK_UN) {
        errno = EIO;
        return -1;
    }
    return 0;
}

static void test_unlock_eintr_retry(void)
{
    g_unlock_calls = 0;
    g_unlock_eintr_remaining = 2;

    int rc = loraham_flock_release(3, fake_flock_unlock_eintr);

    expect_int("unlock EINTR retried until success", rc, 0);
    expect_int("unlock retried exactly 3 calls", g_unlock_calls, 3);
}

static void test_unlock_hard_failure_reported(void)
{
    int rc = loraham_flock_release(3, fake_flock_unlock_hardfail);

    expect_int("unlock hard failure reported (-1)", rc, -1);
}

/* End-to-end: a hard unlock failure in spiEndTransaction must be fatal via the
 * lock-error exit path (never silently continue believing the lock is gone). */
static void test_unlock_hard_failure_is_fatal(void)
{
    pid_t pid = fork();

    if (pid == 0) {
        /* g_tmpdir env is inherited, so the lock file is established. */
        LockingPiHal hal(0, 2000000, 0, 0, fake_flock_unlock_hardfail);
        hal.spiBeginTransaction();   /* LOCK_EX faked ok */
        hal.spiEndTransaction();     /* LOCK_UN faked hard-fail -> must _exit(4) */
        _exit(0);                    /* reached only if NOT fatal -> FAILED */
    }

    int status = 0;
    waitpid(pid, &status, 0);
    expect_int("hard unlock failure exits via lock-error",
               (WIFEXITED(status) && WEXITSTATUS(status) == LORAHAM_EXIT_RUNTIME_RADIO_IO_ERROR)
                   ? 1 : 0, 1);
}

int main(void)
{
    /* Isolate the lock file in a private temp dir. */
    snprintf(g_tmpdir, sizeof(g_tmpdir), "/tmp/loraham-test-%d", (int)getpid());
    mkdir(g_tmpdir, 0755);
    setenv("LORAHAM_RUNTIME_DIR", g_tmpdir, 1);

    test_basic_lock_release();
    test_recursion_guard();
    test_two_instances_share_one_lock();
    test_cross_process_exclusion();
    test_fail_closed_when_dir_unusable();
    test_no_transaction_without_lock();
    test_flock_eintr_retry();
    test_flock_hard_failure();
    test_flock_deadline_recovers();
    test_flock_deadline_expires();
    test_flock_deadline_hard_failure();
    test_unlock_eintr_retry();
    test_unlock_hard_failure_reported();
    test_unlock_hard_failure_is_fatal();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
