#include "../daemon_gpio_lock.h"
#include "../loraham_runtime.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

/* --- Cross-process GPIO ownership tests (audit P1-1) ----------------------- */
/*
 * Uses a private LORAHAM_RUNTIME_DIR. flock is per open-file-description, so
 * a second open+flock on the same lock file conflicts even within one
 * process — that models the second band process.
 */

static int g_ok = 0;
static int g_fail = 0;

static char g_dir[128];

static void expect_int(const char *name, long actual, long expected)
{
    if (actual == expected) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: expected %ld, got %ld\n", name, expected, actual);
    }
}

static int probe_pin_free(int pin)
{
    char path[192];
    int fd;
    int rc;

    snprintf(path, sizeof(path), "%s/gpio%d.lock", g_dir, pin);
    fd = open(path, O_RDWR | O_CREAT, 0660);
    if (fd < 0)
        return -1;

    rc = flock(fd, LOCK_EX | LOCK_NB);
    if (rc == 0)
        flock(fd, LOCK_UN);
    close(fd);

    return rc == 0 ? 1 : 0;
}

static void test_acquire_and_conflict(void)
{
    const int pins[] = { 13, 5, 24, 5, -1 }; /* unsorted, dup, NC */

    expect_int("acquire succeeds",
               daemon_gpio_locks_acquire(pins, 5) ? 1 : 0, 1);
    expect_int("three distinct pins held",
               (long)daemon_gpio_locks_held(), 3);
    expect_int("pin 5 is exclusively held", probe_pin_free(5), 0);
    expect_int("pin 13 is exclusively held", probe_pin_free(13), 0);
    expect_int("unrelated pin stays free", probe_pin_free(19), 1);

    daemon_gpio_locks_release();
    expect_int("release frees pin 5", probe_pin_free(5), 1);
    expect_int("release clears held count",
               (long)daemon_gpio_locks_held(), 0);
}

static void test_conflict_fails_closed(void)
{
    char path[192];
    const int mine[] = { 6 };
    const int theirs[] = { 12, 6 };

    /* "Other process": hold gpio6.lock via a raw descriptor. */
    snprintf(path, sizeof(path), "%s/gpio6.lock", g_dir);
    int fd = open(path, O_RDWR | O_CREAT, 0660);
    if (fd < 0 || flock(fd, LOCK_EX | LOCK_NB) != 0) {
        g_fail++;
        printf("[FAIL] conflict setup\n");
        if (fd >= 0)
            close(fd);
        return;
    }
    (void)mine;

    expect_int("conflicting acquire fails",
               daemon_gpio_locks_acquire(theirs, 2) ? 1 : 0, 0);
    expect_int("failed acquire holds nothing",
               (long)daemon_gpio_locks_held(), 0);
    /* Pin 12 must have been rolled back, not leaked. */
    expect_int("non-conflicting pin rolled back", probe_pin_free(12), 1);

    flock(fd, LOCK_UN);
    close(fd);

    expect_int("acquire succeeds after peer release",
               daemon_gpio_locks_acquire(theirs, 2) ? 1 : 0, 1);
    daemon_gpio_locks_release();
}

/* Audit item 2: locks are acquired BEFORE the hardware-init hook; a failed
 * acquisition never reaches hw_init; a failing hw_init releases the locks. */
static int g_hw_init_calls = 0;
static int g_hw_init_result = 0;
static long g_hw_init_locks_seen = -1;

static int fake_hw_init(void)
{
    g_hw_init_calls++;
    g_hw_init_locks_seen = (long)daemon_gpio_locks_held();
    return g_hw_init_result;
}

static void test_claim_then_ordering(void)
{
    const int pins[] = { 21, 16 };

    g_hw_init_calls = 0;
    g_hw_init_result = 0;
    g_hw_init_locks_seen = -1;

    expect_int("claim-then: success path",
               daemon_gpio_locks_claim_then(pins, 2, fake_hw_init), 0);
    expect_int("claim-then: hw init ran once", g_hw_init_calls, 1);
    expect_int("claim-then: locks held BEFORE hw init",
               g_hw_init_locks_seen, 2);
    expect_int("claim-then: locks still held after success",
               (long)daemon_gpio_locks_held(), 2);
    daemon_gpio_locks_release();

    /* Failing hw init: locks acquired first, then rolled back. */
    g_hw_init_calls = 0;
    g_hw_init_result = 7;
    expect_int("claim-then: hw failure reported as HW_FAILED",
               daemon_gpio_locks_claim_then(pins, 2, fake_hw_init),
               DAEMON_GPIO_CLAIM_HW_FAILED);
    expect_int("claim-then: hw init ran", g_hw_init_calls, 1);
    expect_int("claim-then: locks released on hw failure",
               (long)daemon_gpio_locks_held(), 0);
    g_hw_init_result = 0;
}

static void test_claim_then_conflict_skips_hw_init(void)
{
    char path[192];
    const int pins[] = { 21, 16 };

    /* Peer holds pin 16. */
    snprintf(path, sizeof(path), "%s/gpio16.lock", g_dir);
    int fd = open(path, O_RDWR | O_CREAT, 0660);
    if (fd < 0 || flock(fd, LOCK_EX | LOCK_NB) != 0) {
        g_fail++;
        printf("[FAIL] claim-then conflict setup\n");
        if (fd >= 0)
            close(fd);
        return;
    }

    g_hw_init_calls = 0;
    expect_int("claim-then: conflict returns LOCK_FAILED",
               daemon_gpio_locks_claim_then(pins, 2, fake_hw_init),
               DAEMON_GPIO_CLAIM_LOCK_FAILED);
    expect_int("claim-then: hw init NOT called on conflict",
               g_hw_init_calls, 0);
    expect_int("claim-then: nothing held after conflict",
               (long)daemon_gpio_locks_held(), 0);

    flock(fd, LOCK_UN);
    close(fd);
}

int main(void)
{
    snprintf(g_dir, sizeof(g_dir), "/tmp/loraham-gpiolock-%d", (int)getpid());
    if (mkdir(g_dir, 0700) != 0) {
        printf("[FAIL] test dir setup\n");
        printf("\nSummary: ok=0 fail=1\n");
        return 1;
    }
    setenv("LORAHAM_RUNTIME_DIR", g_dir, 1);

    test_acquire_and_conflict();
    test_conflict_fails_closed();
    test_claim_then_ordering();
    test_claim_then_conflict_skips_hw_init();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
