#include "daemon_gpio_lock.h"

#include <algorithm>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

#include "loraham_runtime.h"

/* --- Cross-process GPIO ownership ---------------------------- */

#define DAEMON_GPIO_LOCK_MAX 16

static int g_gpio_lock_fds[DAEMON_GPIO_LOCK_MAX];
static int g_gpio_lock_pins[DAEMON_GPIO_LOCK_MAX];
static size_t g_gpio_lock_count = 0;

size_t daemon_gpio_locks_held(void)
{
    return g_gpio_lock_count;
}

void daemon_gpio_locks_release(void)
{
    for (size_t i = 0; i < g_gpio_lock_count; i++) {
        /* close() releases the flock; best-effort at teardown. */
        close(g_gpio_lock_fds[i]);
    }
    g_gpio_lock_count = 0;
}

bool daemon_gpio_locks_acquire(const int *pins, size_t count)
{
    int sorted[DAEMON_GPIO_LOCK_MAX];
    size_t n = 0;

    if (g_gpio_lock_count > 0) {
        fprintf(stderr, "[GPIO] error: pin locks already held\n");
        return false;
    }

    for (size_t i = 0; i < count; i++) {
        if (!pins || pins[i] < 0)
            continue;
        if (n >= DAEMON_GPIO_LOCK_MAX) {
            fprintf(stderr, "[GPIO] error: too many pin locks\n");
            return false;
        }
        sorted[n++] = pins[i];
    }

    /* Ascending order (deadlock-free between processes), duplicates out. */
    std::sort(sorted, sorted + n);
    n = (size_t)(std::unique(sorted, sorted + n) - sorted);

    int dirfd = loraham_open_runtime_dir();
    if (dirfd < 0) {
        /* Fail closed: no trusted lock dir means no ownership guarantee. */
        fprintf(stderr, "[GPIO] error: lock directory unusable\n");
        return false;
    }

    for (size_t i = 0; i < n; i++) {
        char name[32];

        snprintf(name, sizeof(name), "gpio%d.lock", sorted[i]);

        int fd = loraham_open_lock_file_at(dirfd, name);
        if (fd < 0) {
            close(dirfd);
            daemon_gpio_locks_release();
            return false;
        }

        int rc;
        do {
            rc = flock(fd, LOCK_EX | LOCK_NB);
        } while (rc < 0 && errno == EINTR);

        if (rc < 0) {
            if (errno == EWOULDBLOCK)
                fprintf(stderr,
                        "[GPIO] error: GPIO %d already claimed by another "
                        "process\n", sorted[i]);
            else
                fprintf(stderr, "[GPIO] error: lock for GPIO %d: %s\n",
                        sorted[i], strerror(errno));
            close(fd);
            close(dirfd);
            daemon_gpio_locks_release();
            return false;
        }

        g_gpio_lock_fds[g_gpio_lock_count] = fd;
        g_gpio_lock_pins[g_gpio_lock_count] = sorted[i];
        g_gpio_lock_count++;
    }

    close(dirfd);

    if (g_gpio_lock_count > 0) {
        fprintf(stderr, "[GPIO] pin locks held:");
        for (size_t i = 0; i < g_gpio_lock_count; i++)
            fprintf(stderr, " %d", g_gpio_lock_pins[i]);
        fprintf(stderr, "\n");
    }

    return true;
}

int daemon_gpio_locks_claim_then(const int *pins, size_t count,
                                 int (*hw_init)(void))
{
    if (!daemon_gpio_locks_acquire(pins, count))
        return DAEMON_GPIO_CLAIM_LOCK_FAILED;

    int rc = hw_init ? hw_init() : 0;
    if (rc != 0) {
        daemon_gpio_locks_release();
        return DAEMON_GPIO_CLAIM_HW_FAILED;
    }

    return 0;
}
