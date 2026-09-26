#ifndef COMMON_LORADAEMON_TEST_H
#define COMMON_LORADAEMON_TEST_H

/*
 * Common helpers for loraham_daemon integration tests.
 *
 * Each test binary starts its own daemon instance, waits for the public
 * Unix sockets and then tests only the external interface.
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <poll.h>
#include <regex.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../daemon_protocol.h"

/* Test-side socket paths: the harness runs the daemon with
 * LORAHAM_SOCKET_DIR=/tmp (see ensure_test_runtime_dir), so clients connect
 * under /tmp regardless of the production /run/loraham defaults. */
#undef SOCK_DATA_433
#undef SOCK_DATA_868
#undef SOCK_DATA_FRAMED_433
#undef SOCK_DATA_FRAMED_868
#undef SOCK_CONF_433
#undef SOCK_CONF_868
#define SOCK_DATA_433 "/tmp/" DATA433_SOCKET_NAME
#define SOCK_DATA_868 "/tmp/" DATA868_SOCKET_NAME
#define SOCK_DATA_FRAMED_433 "/tmp/" DATA433_FRAMED_SOCKET_NAME
#define SOCK_DATA_FRAMED_868 "/tmp/" DATA868_FRAMED_SOCKET_NAME
#define SOCK_CONF_433 "/tmp/" CONF433_SOCKET_NAME
#define SOCK_CONF_868 "/tmp/" CONF868_SOCKET_NAME

#if defined(__GNUC__)
#define TEST_UNUSED __attribute__((unused))
#else
#define TEST_UNUSED
#endif


/* --- Small utility macros --- */

#define ARRAY_LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* --- Conservative integration-test timeouts --- */

#define DEFAULT_SOCKET_TIMEOUT_MS 8000
#define DEFAULT_RSSI_TIMEOUT_MS   4000

/* --- Test return codes --- */

#define TEST_PASS   0
#define TEST_FAIL  -1
#define TEST_SKIP   1

/* --- Per-binary summary counters --- */

static TEST_UNUSED int g_ok = 0;
static TEST_UNUSED int g_fail = 0;
static TEST_UNUSED int g_skip = 0;
static TEST_UNUSED int g_xfail = 0;
static TEST_UNUSED int g_xpass = 0;

/* --- Daemon process owned by the current test binary --- */

static TEST_UNUSED pid_t g_daemon_pid = -1;
static TEST_UNUSED pid_t g_daemon_pid_868 = -1;

/* --- Known-good interface configs used by multiple tests --- */

static TEST_UNUSED const char *lora_cfg_433 =
    "SET MODE=LORA FREQ=433.775 SF=12 BW=125 CR=5 "
    "CRC=1 PREAMBLE=8 SYNC=0x12 LDRO=1 POWER=17\n";

static TEST_UNUSED const char *lora_cfg_868 =
    "SET MODE=LORA FREQ=869.525 SF=11 BW=250 CR=5 "
    "CRC=1 PREAMBLE=16 SYNC=0x2B LDRO=AUTO POWER=10\n";

static TEST_UNUSED const char *fsk_cfg_433 =
    "SET MODE=FSK FREQ=433.775 BR=9.6 FREQDEV=3.0 "
    "RXBW=20.8 SHAPING=0.5 ENCODING=2 "
    "PREAMBLE=32 SYNC=0x2DD4 POWER=10\n";

static TEST_UNUSED const char *fsk_cfg_868 =
    "SET MODE=FSK FREQ=869.525 BR=9.6 FREQDEV=10.0 "
    "RXBW=25.0 SHAPING=0.5 ENCODING=2 "
    "PREAMBLE=32 SYNC=0x2DD4 POWER=10\n";

/* Fast, low-power FSK setup for optional RF-TX smoke tests. */
static TEST_UNUSED const char *fsk_fast_433 =
    "SET MODE=FSK FREQ=433.775 BR=100 FREQDEV=10.0 "
    "RXBW=250.0 SHAPING=0.5 ENCODING=2 "
    "PREAMBLE=32 SYNC=0x2DD4 POWER=0\n";

static TEST_UNUSED const char *fsk_fast_868 =
    "SET MODE=FSK FREQ=869.525 BR=100 FREQDEV=10.0 "
    "RXBW=250.0 SHAPING=0.5 ENCODING=2 "
    "PREAMBLE=32 SYNC=0x2DD4 POWER=0\n";

/* --- Time and logging helpers --- */

static TEST_UNUSED long now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)(ts.tv_sec * 1000L + ts.tv_nsec / 1000000L);
}

static TEST_UNUSED void fail_msg(const char *fmt, ...)
{
    va_list ap;

    printf("[FAIL] ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

static TEST_UNUSED void info_msg(const char *fmt, ...)
{
    va_list ap;

    printf("[INFO] ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

/* --- File/socket helpers --- */

static TEST_UNUSED int path_exists(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0;
}

static TEST_UNUSED int connect_unix_once(const char *path)
{
    int fd;
    struct sockaddr_un addr;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static TEST_UNUSED int connect_unix_retry(const char *path, int timeout_ms)
{
    long deadline = now_ms() + timeout_ms;

    while (now_ms() < deadline) {
        int fd = connect_unix_once(path);
        if (fd >= 0)
            return fd;

        usleep(50000);
    }

    return -1;
}

static TEST_UNUSED int wait_for_socket(const char *path, int timeout_ms)
{
    long deadline = now_ms() + timeout_ms;

    while (now_ms() < deadline) {
        if (path_exists(path)) {
            int fd = connect_unix_retry(path, 300);
            if (fd >= 0) {
                close(fd);
                return 0;
            }
        }

        usleep(100000);
    }

    return -1;
}

static TEST_UNUSED int wait_all_sockets(int timeout_ms)
{
    const char *paths[] = {
        SOCK_DATA_433,
        SOCK_DATA_868,
        SOCK_CONF_433,
        SOCK_CONF_868
    };

    for (int i = 0; i < ARRAY_LEN(paths); i++) {
        if (wait_for_socket(paths[i], timeout_ms) < 0) {
            fail_msg("socket not ready: %s", paths[i]);
            return TEST_FAIL;
        }
    }

    return TEST_PASS;
}

/* Write a complete buffer to a blocking Unix socket. */
static TEST_UNUSED int write_all(int fd, const void *buf, size_t len)
{
    const uint8_t *p = (const uint8_t *)buf;

    while (len > 0) {
        ssize_t n = write(fd, p, len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }

        if (n == 0)
            return -1;

        p += n;
        len -= (size_t)n;
    }

    return 0;
}

/* --- Config socket helpers --- */

static TEST_UNUSED int send_conf_path(const char *path, const char *cmd)
{
    int fd = connect_unix_retry(path, 2000);

    if (fd < 0)
        return TEST_FAIL;

    if (write_all(fd, cmd, strlen(cmd)) < 0) {
        close(fd);
        return TEST_FAIL;
    }

    /* Give the daemon one loop cycle to apply the command. */
    usleep(150000);
    close(fd);
    return TEST_PASS;
}

static TEST_UNUSED int send_conf_band(int band, const char *cmd)
{
    if (band == 433)
        return send_conf_path(SOCK_CONF_433, cmd);

    if (band == 868)
        return send_conf_path(SOCK_CONF_868, cmd);

    return TEST_FAIL;
}

/* --- Line reader for RSSI/CAD-style text output --- */

static TEST_UNUSED int line_matches_regex(const char *line, const char *pattern)
{
    regex_t rx;
    int ret;

    if (regcomp(&rx, pattern, REG_EXTENDED | REG_NOSUB) != 0)
        return 0;

    ret = regexec(&rx, line, 0, NULL, 0);
    regfree(&rx);

    return ret == 0;
}

static TEST_UNUSED int wait_for_matching_line(int fd, const char *pattern,
                                  int timeout_ms, char *out, size_t out_size)
{
    char buf[4096];
    char line[512];
    size_t line_len = 0;
    long deadline = now_ms() + timeout_ms;

    memset(line, 0, sizeof(line));

    while (now_ms() < deadline) {
        struct pollfd pfd;
        int remaining = (int)(deadline - now_ms());
        int pr;

        if (remaining < 1)
            remaining = 1;

        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        pr = poll(&pfd, 1, remaining);
        if (pr < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }

        if (pr == 0)
            continue;

        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
            return -1;

        if (pfd.revents & POLLIN) {
            ssize_t n = read(fd, buf, sizeof(buf));
            if (n <= 0)
                return -1;

            for (ssize_t i = 0; i < n; i++) {
                char c = buf[i];

                if (c == '\n') {
                    line[line_len] = '\0';

                    while (line_len > 0 &&
                           (line[line_len - 1] == '\r' ||
                            line[line_len - 1] == ' ' ||
                            line[line_len - 1] == '\t')) {
                        line[--line_len] = '\0';
                    }

                    if (line_matches_regex(line, pattern)) {
                        if (out && out_size > 0) {
                            size_t copy_len = strlen(line);

                            if (copy_len >= out_size)
                                copy_len = out_size - 1;

                            memcpy(out, line, copy_len);
                            out[copy_len] = '\0';
                        }
                        return 0;
                    }

                    line_len = 0;
                    line[0] = '\0';
                } else {
                    if (line_len + 1 < sizeof(line))
                        line[line_len++] = c;
                    else
                        line_len = 0;
                }
            }
        }
    }

    return -1;
}

/* Enable RSSI stream and wait for one well-formed line. */
static TEST_UNUSED int assert_getrssi_stream(int band)
{
    const char *path = (band == 433) ? SOCK_CONF_433 : SOCK_CONF_868;
    int fd = connect_unix_retry(path, 2000);
    char line[128];

    if (fd < 0)
        return TEST_FAIL;

    if (write_all(fd, "SET GETRSSI=1\n", strlen("SET GETRSSI=1\n")) < 0) {
        close(fd);
        return TEST_FAIL;
    }

    if (wait_for_matching_line(fd,
                               "^RSSI=-?[0-9]+\\.[0-9][0-9]$",
                               DEFAULT_RSSI_TIMEOUT_MS,
                               line,
                               sizeof(line)) < 0) {
        close(fd);
        return TEST_FAIL;
    }

    printf("       %d RSSI line: %s\n", band, line);

    (void)write_all(fd, "SET GETRSSI=0\n", strlen("SET GETRSSI=0\n"));
    usleep(200000);

    close(fd);
    return TEST_PASS;
}

/* --- Daemon lifecycle helpers --- */

/*
 * Point the spawned daemon at a writable, trusted (0700, owned by the test user)
 * lock directory via LORAHAM_RUNTIME_DIR. The production default
 * /run/lock/loraham requires a root-owned directory, which a non-root test
 * runner cannot satisfy; the dev override relaxes the root-owner requirement
 * while still validating against symlink and group/world-writable directories.
 * Set in the parent so the value is inherited across execl().
 */
static TEST_UNUSED void ensure_test_runtime_dir(void)
{
    static int done = 0;
    char dir[128];

    if (done)
        return;

    if (!getenv("LORAHAM_RUNTIME_DIR")) {
        snprintf(dir, sizeof(dir), "/tmp/loraham-test-%d", (int)getpid());
        mkdir(dir, 0700);
        setenv("LORAHAM_RUNTIME_DIR", dir, 1);
    }

    /* Production sockets live in /run/loraham (root-provisioned); a
     * non-root test runner points daemon AND clients at /tmp via the
     * dev/test-only override. The TEST_SOCK_* macros below must resolve to
     * the same directory. */
    if (!getenv("LORAHAM_SOCKET_DIR"))
        setenv("LORAHAM_SOCKET_DIR", "/tmp", 1);

    done = 1;
}

/*
 * The daemon is single-radio-per-process (--radio is mandatory), so the
 * harness spawns one 433 instance by default; tests that exercise the 868
 * sockets additionally start a second 868 instance, mirroring the production
 * two-unit deployment.
 */
static int start_daemon_band(const char *bin, const char *band, pid_t *out_pid)
{
    pid_t pid;

    ensure_test_runtime_dir();

    pid = fork();

    if (pid < 0)
        return TEST_FAIL;

    if (pid == 0) {
        setpgid(0, 0);
        execl(bin, bin, "--radio", band, (char *)NULL);
        _exit(127);
    }

    *out_pid = pid;
    return TEST_PASS;
}

static TEST_UNUSED int start_daemon(const char *bin)
{
    return start_daemon_band(bin, "433", &g_daemon_pid);
}

static TEST_UNUSED int start_daemon_868(const char *bin)
{
    return start_daemon_band(bin, "868", &g_daemon_pid_868);
}

static void stop_daemon_pid(pid_t *pid)
{
    int status;

    if (*pid <= 0)
        return;

    /* Group-kill with direct-kill fallback: if the child lost the setpgid
     * race the group may not exist yet. */
    if (kill(-*pid, SIGTERM) != 0)
        kill(*pid, SIGTERM);

    for (int i = 0; i < 30; i++) {
        pid_t r = waitpid(*pid, &status, WNOHANG);
        if (r == *pid) {
            *pid = -1;
            return;
        }
        usleep(100000);
    }

    if (kill(-*pid, SIGKILL) != 0)
        kill(*pid, SIGKILL);
    waitpid(*pid, &status, 0);
    *pid = -1;
}

static TEST_UNUSED void stop_daemon(void)
{
    stop_daemon_pid(&g_daemon_pid_868);
    stop_daemon_pid(&g_daemon_pid);
}

static TEST_UNUSED int daemon_alive(void)
{
    if (g_daemon_pid_868 > 0 && kill(g_daemon_pid_868, 0) != 0)
        return 0;

    if (g_daemon_pid <= 0)
        return 1;

    return kill(g_daemon_pid, 0) == 0;
}

static TEST_UNUSED int wait_owned_daemon_exit(int timeout_ms)
{
    int status;
    long deadline = now_ms() + timeout_ms;

    while (now_ms() < deadline) {
        pid_t r;

        if (g_daemon_pid <= 0)
            return TEST_PASS;

        r = waitpid(g_daemon_pid, &status, WNOHANG);
        if (r == g_daemon_pid) {
            g_daemon_pid = -1;
            return TEST_PASS;
        }

        usleep(100000);
    }

    return TEST_FAIL;
}

static TEST_UNUSED int public_sockets_removed(void)
{
    const char *paths[] = {
        SOCK_DATA_433,
        SOCK_DATA_868,
        SOCK_CONF_433,
        SOCK_CONF_868
    };

    for (int i = 0; i < ARRAY_LEN(paths); i++) {
        if (path_exists(paths[i])) {
            fail_msg("socket still exists after shutdown: %s", paths[i]);
            return TEST_FAIL;
        }
    }

    return TEST_PASS;
}

static TEST_UNUSED int wait_client_closed_after_shutdown(int fd, int timeout_ms)
{
    long deadline = now_ms() + timeout_ms;

    while (now_ms() < deadline) {
        struct pollfd pfd;
        char buf[8];
        int remaining = (int)(deadline - now_ms());
        int pr;

        if (remaining < 1)
            remaining = 1;

        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        pr = poll(&pfd, 1, remaining);
        if (pr < 0) {
            if (errno == EINTR)
                continue;
            return TEST_FAIL;
        }

        if (pr == 0)
            continue;

        if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL))
            return TEST_PASS;

        if (pfd.revents & POLLIN) {
            ssize_t n = read(fd, buf, sizeof(buf));
            if (n == 0)
                return TEST_PASS;
            if (n < 0 && errno != EINTR)
                return TEST_PASS;
        }
    }

    return TEST_FAIL;
}

/* --- CLI smoke helper --- */

static TEST_UNUSED int run_cli_capture(const char *bin,
                                       const char *arg1,
                                       const char *arg2,
                                       char *out,
                                       size_t out_size,
                                       int *exit_code)
{
    pid_t pid;
    int pipefd[2];
    ssize_t n;
    int status;

    if (!bin)
        return TEST_SKIP;

    if (pipe(pipefd) < 0)
        return TEST_FAIL;

    pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return TEST_FAIL;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        if (arg2)
            execl(bin, bin, arg1, arg2, (char *)NULL);
        else if (arg1)
            execl(bin, bin, arg1, (char *)NULL);
        else
            execl(bin, bin, (char *)NULL);

        _exit(127);
    }

    close(pipefd[1]);

    /* Read to EOF: with line-buffered child stdout the output arrives in
     * multiple pipe chunks — a single read() raced them (latent bug hidden
     * by the previous full buffering). */
    {
        size_t total = 0;

        for (;;) {
            n = read(pipefd[0], out + total, out_size - 1 - total);
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                break;
            total += (size_t)n;
            if (total >= out_size - 1)
                break;
        }
        close(pipefd[0]);

        waitpid(pid, &status, 0);

        if (n < 0)
            return TEST_FAIL;

        out[total] = '\0';
    }

    if (!WIFEXITED(status))
        return TEST_FAIL;

    *exit_code = WEXITSTATUS(status);
    return TEST_PASS;
}

static TEST_UNUSED int test_cli_invalid_option(const char *bin)
{
    char out[1024];
    int exit_code = 0;
    int ret = run_cli_capture(bin, "-x", NULL, out, sizeof(out), &exit_code);

    if (ret != TEST_PASS)
        return ret;

    if (exit_code == 0)
        return TEST_FAIL;

    if (strstr(out, "Usage:") == NULL &&
        strstr(out, "invalid option") == NULL)
        return TEST_FAIL;

    return TEST_PASS;
}

static TEST_UNUSED int test_cli_help_option(const char *bin,
                                            const char *option)
{
    char out[1024];
    int exit_code = 0;
    int ret = run_cli_capture(bin, option, "--help", out, sizeof(out), &exit_code);

    if (ret != TEST_PASS)
        return ret;

    if (exit_code != 0)
        return TEST_FAIL;

    if (strstr(out, "Usage:") == NULL)
        return TEST_FAIL;

    return TEST_PASS;
}

/* --- Test runner output helpers --- */

static TEST_UNUSED void run_test(const char *name, int (*fn)(void))
{
    int ret;

    printf("[TEST] %s\n", name);
    ret = fn();

    if (ret == TEST_PASS) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else if (ret == TEST_SKIP) {
        g_skip++;
        printf("[SKIP] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s\n", name);
    }
}

static TEST_UNUSED void run_xfail_test(const char *name, int (*fn)(void))
{
    int ret;

    printf("[TEST] %s\n", name);
    ret = fn();

    if (ret == TEST_PASS) {
        g_xpass++;
        printf("[XPASS] %s\n", name);
    } else {
        g_xfail++;
        printf("[XFAIL] %s\n", name);
    }
}

static TEST_UNUSED int print_summary(void)
{
    printf("\nSummary: ok=%d fail=%d skip=%d xfail=%d xpass=%d\n",
           g_ok, g_fail, g_skip, g_xfail, g_xpass);

    return g_fail ? 1 : 0;
}

/* --- Radio-hardware precondition --- */

/*
 * Some tests drive a live daemon, and a daemon only stays up once its radio
 * reports ready.  Without an SPI device no radio can even be probed - lgSpiOpen
 * fails and the band aborts - so on a machine that has none, which is every
 * hosted CI runner, there is nothing for those tests to observe.
 *
 * The check is deliberately for the *precondition* and not for "the daemon did
 * not come up": wherever an SPI device exists the tests run for real, and a
 * daemon that then fails to start still FAILs.  So this reports an honest SKIP
 * on a machine without radio hardware without being able to hide a regression
 * on a machine with it.
 */
static TEST_UNUSED int radio_hardware_missing(void)
{
    return !path_exists("/dev/spidev0.0") && !path_exists("/dev/spidev0.1");
}

static TEST_UNUSED int test_needs_radio_hardware(void)
{
    return TEST_SKIP;
}

/* Record the skip and finish the binary.  Whatever ran before the daemon start
 * - CLI handling, the fail-closed exit paths - is already counted, so the
 * caller returns this as its exit code and the runner still gets a summary. */
static TEST_UNUSED int skip_live_daemon_tests(void)
{
    info_msg("no SPI device (/dev/spidev0.0, /dev/spidev0.1)");
    run_test("live-daemon tests require radio hardware", test_needs_radio_hardware);
    return print_summary();
}

static TEST_UNUSED void usage_common(const char *argv0)
{
    printf("Usage: %s --bin ./loraham_daemon\n", argv0);
}

#endif
