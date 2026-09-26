#include "daemon_instance_lock.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include "loraham_runtime.h"
#include "daemon_log.h"
#include "daemon_band.h"

/*
 * Lock-ordering contract: one instance lock per process (the selected band's
 * instance-<band>.lock), then the per-transaction SPI lock (spi0). The SPI
 * lock is only ever taken inside a single SPI transaction, never during this
 * long-lived instance setup, so it can never be held while we block on an
 * instance lock.
 *
 * Instance locks are non-blocking (LOCK_NB): a second same-band daemon fails
 * fast instead of queueing.
 */

static int g_lock_fd = -1;

static int instance_lock_claim(const char *band, int *out_fd)
{
    char name[64];
    int dirfd;
    int fd;

    /* Validate the trusted lock directory (root-owned in production, no symlink,
     * not group/world-writable), then create the lock file relative to it. The
     * directory is never silently created in production. */
    dirfd = loraham_open_runtime_dir();
    if (dirfd < 0)
        return LORAHAM_EXIT_LOCK_ERROR;   /* fail closed */

    snprintf(name, sizeof(name), "instance-%s.lock", band);

    fd = loraham_open_lock_file_at(dirfd, name);
    close(dirfd);
    if (fd < 0)
        return LORAHAM_EXIT_LOCK_ERROR;   /* fail closed */

    if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
        int err = errno;

        close(fd);

        if (err == EWOULDBLOCK) {
            printf("[LOCK] band %s is already run by another instance "
                   "- exiting.\n", band);
            return LORAHAM_EXIT_INSTANCE_BUSY;
        }

        printf("[LOCK] error: flock(instance-%s) failed: %s\n",
               band, strerror(err));
        return LORAHAM_EXIT_LOCK_ERROR;   /* fail closed */
    }

    *out_fd = fd;
    daemon_log("instance lock acquired: %s/instance-%s.lock",
               loraham_runtime_dir(), band);
    return 0;
}

int daemon_instance_lock_acquire(void)
{
    /* Exactly one band per process; claim only its lock. */
    return instance_lock_claim(daemon_band()->tag, &g_lock_fd);
}

void daemon_instance_lock_release(void)
{
    /* Closing the descriptor drops the advisory lock; the explicit LOCK_UN
     * is belt-and-suspenders. */
    if (g_lock_fd >= 0) {
        flock(g_lock_fd, LOCK_UN);
        close(g_lock_fd);
        g_lock_fd = -1;
    }
}
