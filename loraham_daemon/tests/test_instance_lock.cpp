/*
 * Unit tests for the per-band instance-ownership locks (daemon_instance_lock).
 *
 * Covers: 433-only / 868-only ownership, duplicate-band rejection,
 * release-ordering (the shutdown/restart race barrier), release idempotency,
 * and shared-lock inode stability.
 *
 * No radio hardware is required. The lock directory is isolated via
 * LORAHAM_RUNTIME_DIR.
 */

#include "../daemon_instance_lock.h"
#include "../loraham_runtime.h"
#include "../daemon_band.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

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

static void band_path(const char *band, char *out, size_t n)
{
    snprintf(out, n, "%s/instance-%s.lock", loraham_runtime_dir(), band);
}

/* 1 if the band lock is free (grabbed+released via an independent fd), 0 held. */
static int probe_free(const char *band)
{
    char p[256];
    int fd;
    int r;

    band_path(band, p, sizeof(p));
    fd = open(p, O_CREAT | O_RDWR, 0660);
    if (fd < 0)
        return -1;

    r = flock(fd, LOCK_EX | LOCK_NB);
    if (r == 0) {
        flock(fd, LOCK_UN);
        close(fd);
        return 1;
    }
    close(fd);
    return 0;
}

/* Hold a band lock via an independent descriptor (simulates a running peer). */
static int hold_band(const char *band)
{
    char p[256];
    int fd;

    band_path(band, p, sizeof(p));
    fd = open(p, O_CREAT | O_RDWR, 0660);
    if (fd < 0)
        return -1;

    if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void unhold(int fd)
{
    if (fd >= 0) {
        flock(fd, LOCK_UN);
        close(fd);
    }
}

static void test_433_only_ownership(void)
{
    daemon_band_resolve(RADIO_BAND_433);

    expect_int("433-only acquire ok", daemon_instance_lock_acquire(), 0);
    expect_int("433 lock held", probe_free("433"), 0);
    expect_int("868 lock untouched", probe_free("868"), 1);

    daemon_instance_lock_release();
    expect_int("433 lock free after release", probe_free("433"), 1);
}

static void test_868_only_ownership(void)
{
    daemon_band_resolve(RADIO_BAND_868);

    expect_int("868-only acquire ok", daemon_instance_lock_acquire(), 0);
    expect_int("868 lock held", probe_free("868"), 0);
    expect_int("433 lock untouched", probe_free("433"), 1);

    daemon_instance_lock_release();
    expect_int("868 lock free after release", probe_free("868"), 1);
}

static void test_duplicate_433_rejected(void)
{
    int peer = hold_band("433");                       /* simulate running A */
    expect_int("peer holds 433", peer >= 0 ? 1 : 0, 1);

    daemon_band_resolve(RADIO_BAND_433);
    expect_int("duplicate 433 rejected (BUSY)",
               daemon_instance_lock_acquire(), LORAHAM_EXIT_INSTANCE_BUSY);

    daemon_instance_lock_release();                    /* no-op; nothing held */
    unhold(peer);
    expect_int("433 free after peer leaves", probe_free("433"), 1);
}

static void test_duplicate_868_rejected(void)
{
    int peer = hold_band("868");
    expect_int("peer holds 868", peer >= 0 ? 1 : 0, 1);

    daemon_band_resolve(RADIO_BAND_868);
    expect_int("duplicate 868 rejected (BUSY)",
               daemon_instance_lock_acquire(), LORAHAM_EXIT_INSTANCE_BUSY);

    daemon_instance_lock_release();
    unhold(peer);
    expect_int("868 free after peer leaves", probe_free("868"), 1);
}

/* The shutdown/restart race barrier: a new instance can only take the lock
 * after the old instance releases it (which the daemon does only after socket
 * cleanup). */
static void test_release_unblocks_restart(void)
{
    daemon_band_resolve(RADIO_BAND_433);

    expect_int("first acquire ok", daemon_instance_lock_acquire(), 0);
    expect_int("while held, restart would be blocked", probe_free("433"), 0);

    daemon_instance_lock_release();
    expect_int("after release, restart can acquire", probe_free("433"), 1);

    /* And a fresh acquire now succeeds. */
    expect_int("re-acquire after release ok", daemon_instance_lock_acquire(), 0);
    daemon_instance_lock_release();
}

static void test_release_idempotent(void)
{
    daemon_instance_lock_release();   /* nothing held */
    daemon_band_resolve(RADIO_BAND_433);
    expect_int("acquire ok", daemon_instance_lock_acquire(), 0);
    daemon_instance_lock_release();
    daemon_instance_lock_release();   /* double release safe */
    expect_int("433 free after double release", probe_free("433"), 1);
}

/* Inode stability: the lock file must keep one inode across reopen, and deleting
 * it (as a stopped systemd RuntimeDirectory would) changes the inode -- which is
 * exactly why the shared lock dir must be durable (tmpfiles.d), not per-unit. */
static void test_inode_stability(void)
{
    char p[256];
    struct stat s1, s2, s3;
    int fd1, fd2, fd3, keep_old;

    snprintf(p, sizeof(p), "%s/spi0.lock", loraham_runtime_dir());

    fd1 = open(p, O_CREAT | O_RDWR, 0660);
    fstat(fd1, &s1);

    fd2 = open(p, O_CREAT | O_RDWR, 0660);
    fstat(fd2, &s2);
    close(fd2);
    expect_int("lock inode stable across reopen", s1.st_ino == s2.st_ino, 1);

    /* Keep the old inode alive so it cannot be immediately reused, then delete
     * and recreate the path (models a RuntimeDirectory removal). */
    keep_old = open(p, O_RDONLY);
    unlink(p);
    fd3 = open(p, O_CREAT | O_RDWR, 0660);
    fstat(fd3, &s3);
    expect_int("deleting the lock file changes the inode",
               s1.st_ino != s3.st_ino, 1);

    close(fd1);
    close(fd3);
    if (keep_old >= 0)
        close(keep_old);
}

int main(void)
{
    char dir[256];

    snprintf(dir, sizeof(dir), "/tmp/loraham-itest-%d", (int)getpid());
    mkdir(dir, 0755);
    setenv("LORAHAM_RUNTIME_DIR", dir, 1);

    test_433_only_ownership();
    test_868_only_ownership();
    test_duplicate_433_rejected();
    test_duplicate_868_rejected();
    test_release_unblocks_restart();
    test_release_idempotent();
    test_inode_stability();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
