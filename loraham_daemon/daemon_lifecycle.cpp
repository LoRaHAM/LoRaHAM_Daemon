#include "daemon_lifecycle.h"

#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <string.h>

/* --- Daemon background mode --------------------------------------------- */

static long daemon_lifecycle_fd_close_limit(void)
{
    struct rlimit limit;
    long open_max;

    if (getrlimit(RLIMIT_NOFILE, &limit) == 0 &&
        limit.rlim_cur != RLIM_INFINITY &&
        limit.rlim_cur <= 1048576) {
        return (long)limit.rlim_cur;
    }

    open_max = sysconf(_SC_OPEN_MAX);
    if (open_max > 0 && open_max <= 1048576)
        return open_max;

    return 1024;
}

static void daemon_lifecycle_close_inherited_fds(void)
{
    DIR *dir = opendir("/proc/self/fd");

    if (dir) {
        int keep_fd = dirfd(dir);
        struct dirent *entry;

        while ((entry = readdir(dir)) != NULL) {
            char *end = NULL;
            long fd;

            errno = 0;
            fd = strtol(entry->d_name, &end, 10);
            if (errno != 0 || !end || *end != '\0')
                continue;

            if (fd > STDERR_FILENO && fd != keep_fd)
                close((int)fd);
        }

        closedir(dir);
        return;
    }

    long max_fd = daemon_lifecycle_fd_close_limit();
    for (long fd = STDERR_FILENO + 1; fd < max_fd; fd++)
        close((int)fd);
}

int daemon_lifecycle_redirect_stdio(const char *log_path)
{
    if (!log_path || log_path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    /* O_NOFOLLOW: the fixed /tmp log path must never follow a
     * symlink planted by another local user; a symlink at the path is a
     * hard error, not a redirect. The verified descriptor is then re-bound
     * to the stdout STREAM via /dev/fd (race-free: it names our open file
     * description, not the path) so the freopen resets FILE buffering —
     * plain dup2 would keep a stale fully-buffered stream that _exit()
     * silently drops. /dev/fd is the standard /proc/self/fd symlink and
     * requires a mounted /proc — always present on the supported Raspberry
     * Pi OS targets; without it the redirect fails closed (daemon mode
     * aborts) rather than logging to the wrong place. */
    int fd = open(log_path,
                  O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW | O_CLOEXEC,
                  0640);
    if (fd < 0)
        return -1;

    char fd_path[64];
    snprintf(fd_path, sizeof(fd_path), "/dev/fd/%d", fd);
    if (!freopen(fd_path, "a", stdout)) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }
    close(fd);

    if (dup2(fileno(stdout), STDERR_FILENO) < 0)
        return -1;

    if (setvbuf(stdout, NULL, _IOLBF, 0) != 0)
        return -1;

    if (setvbuf(stderr, NULL, _IONBF, 0) != 0)
        return -1;

    return 0;
}

void daemon_lifecycle_enter_background(void)
{
    pid_t pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);

    if (setsid() < 0) exit(EXIT_FAILURE);

    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);

    umask(007);
    if (chdir("/") != 0)
        exit(EXIT_FAILURE);

    // Redirect stdio so sockets cannot reuse fd 0, 1 or 2.
    if (!freopen("/dev/null", "r", stdin))
        exit(EXIT_FAILURE);

    if (daemon_lifecycle_redirect_stdio("/tmp/lora_daemon.log") != 0)
        exit(EXIT_FAILURE);

    daemon_lifecycle_close_inherited_fds();

}

/* --- Daemon stop handling ---------------------------------------------- */

static volatile sig_atomic_t daemon_stop_requested_flag = 0;

void daemon_lifecycle_reset_stop(void)
{
    daemon_stop_requested_flag = 0;
}

void daemon_lifecycle_request_stop(int signal_number)
{
    (void)signal_number;
    daemon_stop_requested_flag = 1;
}

int daemon_lifecycle_stop_requested(void)
{
    return daemon_stop_requested_flag ? 1 : 0;
}

DaemonWaitErrorAction daemon_lifecycle_classify_wait_error(int err,
                                                           int stop_requested)
{
    if (err == EINTR)
        return stop_requested ? DAEMON_WAIT_ERROR_STOPPING
                              : DAEMON_WAIT_ERROR_SILENT;

    return DAEMON_WAIT_ERROR_LOG;
}

void daemon_lifecycle_ignore_sigpipe(void)
{
    // Ignore SIGPIPE; closed sockets are handled via write() errors.
    signal(SIGPIPE, SIG_IGN);
}

int daemon_lifecycle_install_signal_handlers(void)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = daemon_lifecycle_request_stop;

    if (sigemptyset(&action.sa_mask) != 0)
        return -1;

    if (sigaction(SIGINT, &action, NULL) != 0)
        return -1;

    if (sigaction(SIGTERM, &action, NULL) != 0)
        return -1;

    return 0;
}
