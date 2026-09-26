#ifndef LORAHAM_RUNTIME_H
#define LORAHAM_RUNTIME_H

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <sys/file.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/*
 * Shared trusted runtime directory for LoRaHAM process-shared locks: the
 * per-band instance-ownership locks and the SPI-bus lock both live here.
 *
 * Production: /run/lock/loraham, provisioned root-owned (0755) by tmpfiles.d so
 * its lifetime is independent of any single daemon instance. It must NOT be a
 * systemd RuntimeDirectory: a per-unit RuntimeDirectory is removed when that
 * unit stops, which would pull the shared lock inode out from under a still
 * running peer and silently break cross-process SPI serialization.
 *
 * The directory is validated before use (see loraham_open_lock_dir): it must be
 * a real directory (not a symlink), not group- or world-writable, and -- for the
 * production default path -- owned by root or by the daemon user itself
 * (the daemon runs unprivileged; tmpfiles provisions the directory for the
 * dedicated `loraham` user). The daemon never silently creates
 * the production directory; it must be pre-provisioned by tmpfiles.d. There is
 * deliberately no implicit /tmp fallback: a lock that cannot be established in
 * the trusted location must fail closed, never open.
 *
 * LORAHAM_RUNTIME_DIR overrides the directory for non-root dev/test runs only.
 * In override mode the directory is created if missing (0700) and the root-owner
 * requirement is relaxed, but it is still validated against symlink and
 * group/world-writable attacks. The production systemd unit does NOT pass this
 * variable (no EnvironmentFile), so it cannot redirect the shared namespace.
 */
#define LORAHAM_RUNTIME_DIR_DEFAULT "/run/lock/loraham"

/* Process exit codes (stable; referenced by the systemd unit). */
#define LORAHAM_EXIT_INSTANCE_BUSY 3  /* same-band instance already running */
#define LORAHAM_EXIT_LOCK_ERROR    4  /* STARTUP prerequisite failed: lock
                                        * infrastructure OR a GPIO open/claim.
                                        * Neither is fixed by trying again, so
                                        * fail closed, not restartable */
#define LORAHAM_EXIT_RUNTIME_RADIO_IO_ERROR 5 /* runtime radio-I/O fatal after
                                               * operation began: SPI, the bus
                                               * lock, or a GPIO call once the
                                               * radio was live. systemd MAY
                                               * restart (Restart=on-failure) */

/* Former name, kept so an out-of-tree caller does not break silently. The
 * concept was widened from "SPI" to "radio I/O" when GPIO failures started
 * exiting through it, which they always did in practice. */
#define LORAHAM_EXIT_RUNTIME_SPI_ERROR LORAHAM_EXIT_RUNTIME_RADIO_IO_ERROR

static inline const char *loraham_runtime_dir(void)
{
    const char *d = getenv("LORAHAM_RUNTIME_DIR");

    return (d && *d) ? d : LORAHAM_RUNTIME_DIR_DEFAULT;
}

/* True when the runtime directory has been overridden (dev/test mode). */
static inline int loraham_runtime_dir_is_override(void)
{
    const char *d = getenv("LORAHAM_RUNTIME_DIR");

    return (d && *d) ? 1 : 0;
}

/*
 * Open and validate the trusted lock directory, returning a directory fd.
 *
 * Opened O_NOFOLLOW so a symlinked directory is rejected. Requires a real
 * directory that is not group- or world-writable; when require_root is set
 * (the production default path) it must also be owned by root. Returns -1 on any
 * failure, with a diagnostic on stderr.
 */
static inline int loraham_open_lock_dir(const char *dir, int require_root)
{
    struct stat st;
    int fd = open(dir, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);

    if (fd < 0) {
        fprintf(stderr, "[LOCK] error: lock directory %s unusable: %s\n",
                dir, strerror(errno));
        return -1;
    }

    if (fstat(fd, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "[LOCK] error: %s is not a directory\n", dir);
        close(fd);
        return -1;
    }

    /* Trusted owner (non-root deployment): the production directory must be
     * owned by root or by the daemon user itself — never a third user, who
     * could otherwise pre-create and manipulate the lock namespace. The
     * daemon deliberately does NOT run as root (hardware access comes from
     * the spi/gpio supplementary groups). */
    if (require_root && st.st_uid != 0 && st.st_uid != geteuid()) {
        fprintf(stderr,
                "[LOCK] error: lock directory %s is owned by neither root nor "
                "the daemon user (uid=%u)\n",
                dir, (unsigned)st.st_uid);
        close(fd);
        return -1;
    }

    if (st.st_mode & (S_IWGRP | S_IWOTH)) {
        fprintf(stderr,
                "[LOCK] error: lock directory %s is group- or world-writable "
                "(mode=%04o)\n", dir, (unsigned)(st.st_mode & 07777));
        close(fd);
        return -1;
    }

    return fd;
}

/*
 * Resolve and validate the runtime lock directory, returning a directory fd.
 *
 * Production (default path): the directory must already exist, root-owned and
 * not group/world-writable -- it is never silently created here. Dev/test
 * (LORAHAM_RUNTIME_DIR set): the directory is created 0700 if missing and the
 * root-owner requirement is relaxed, but symlink and group/world-writable checks
 * still apply. Returns -1 on failure.
 */
static inline int loraham_open_runtime_dir(void)
{
    const char *dir = loraham_runtime_dir();
    int override = loraham_runtime_dir_is_override();

    if (override)
        mkdir(dir, 0700);   /* best-effort for dev; EEXIST is fine */

    return loraham_open_lock_dir(dir, /*require_root=*/!override);
}

/*
 * Create/open a lock file inside the validated directory fd. O_NOFOLLOW rejects
 * a symlink planted at the name, and the result must be a regular file. Returns
 * the fd or -1 with a diagnostic.
 */
static inline int loraham_open_lock_file_at(int dirfd, const char *name)
{
    struct stat st;
    /* Owner-only: instance/SPI/GPIO locks are HARDWARE ownership —
     * a loraham-group socket client must never be able to hold one and
     * block startup or force a runtime SPI timeout. 0600 on create, and
     * fchmod() below also corrects files created by earlier builds. */
    int fd = openat(dirfd, name, O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600);

    if (fd < 0) {
        fprintf(stderr, "[LOCK] error: lock file %s unusable: %s\n",
                name, strerror(errno));
        return -1;
    }

    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        fprintf(stderr, "[LOCK] error: lock file %s is not a regular file\n",
                name);
        close(fd);
        return -1;
    }

    if ((st.st_mode & 0777) != 0600 && fchmod(fd, 0600) != 0) {
        fprintf(stderr, "[LOCK] error: lock file %s cannot be corrected to 0600: %s\n", name, strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

/*
 * Advisory-lock helpers. `fn` is the flock implementation (real flock() in
 * production; a fake in tests). Each retries ONLY on EINTR and returns 0 when
 * the operation is confirmed, or -1 on a hard (non-EINTR) failure with errno
 * preserved. They never report success without the operation having taken
 * effect, so callers treat a non-zero return as "must not proceed".
 */
typedef int (*loraham_flock_fn)(int fd, int operation);

static inline int loraham_flock_acquire_ex(int fd, loraham_flock_fn fn)
{
    int rc;

    do {
        rc = fn(fd, LOCK_EX);
    } while (rc < 0 && errno == EINTR);

    return rc < 0 ? -1 : 0;
}

/*
 * Deadline-bounded exclusive acquisition: polls LOCK_EX|LOCK_NB
 * against a CLOCK_MONOTONIC deadline instead of blocking forever behind a
 * live-but-wedged peer. Returns 0 when held, -1 with errno=ETIMEDOUT on
 * expiry, -1 with errno preserved on any hard failure. Callers must treat
 * every non-zero return as "must not proceed" (fail closed, no unlocked
 * fallback). SPI transactions hold the lock for microseconds–milliseconds;
 * a peer holding it past the deadline is a fault, not contention.
 */
static inline int loraham_flock_acquire_ex_deadline(int fd,
                                                    loraham_flock_fn fn,
                                                    long timeout_ms)
{
    struct timespec start;
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0)
        return -1;

    for (;;) {
        int rc = fn(fd, LOCK_EX | LOCK_NB);

        if (rc == 0)
            return 0;

        if (errno != EINTR && errno != EWOULDBLOCK && errno != EAGAIN)
            return -1;

        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
            return -1;

        long elapsed_ms = (long)(now.tv_sec - start.tv_sec) * 1000L +
                          (long)(now.tv_nsec - start.tv_nsec) / 1000000L;
        if (elapsed_ms >= timeout_ms) {
            errno = ETIMEDOUT;
            return -1;
        }

        struct timespec pause = { 0, 1000000L }; /* 1 ms */
        nanosleep(&pause, NULL);
    }
}

static inline int loraham_flock_release(int fd, loraham_flock_fn fn)
{
    int rc;

    do {
        rc = fn(fd, LOCK_UN);
    } while (rc < 0 && errno == EINTR);

    return rc < 0 ? -1 : 0;
}

#endif
