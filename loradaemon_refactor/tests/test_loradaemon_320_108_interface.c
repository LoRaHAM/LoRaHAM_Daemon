/*
 * test_loradaemon_interface.c
 *
 * Integrationstest fuer loraham_daemon.
 *
 * Kompilieren:
 *   gcc -std=c11 -Wall -Wextra -O2 -o test_loradaemon_interface test_loradaemon_interface.c
 *
 * Gegen laufenden Daemon testen:
 *   ./test_loradaemon_interface
 *
 * Daemon fuer Test starten:
 *   ./test_loradaemon_interface --bin ./loraham_daemon
 *
 * Mit echtem RF-TX-Test:
 *   ./test_loradaemon_interface --rf-tx
 *
 * RX kurz beobachten:
 *   ./test_loradaemon_interface --rx-seconds 15
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
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

#define ARRAY_LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))

#define SOCK_DATA_433 "/tmp/lora433.sock"
#define SOCK_DATA_868 "/tmp/lora868.sock"
#define SOCK_CONF_433 "/tmp/loraconf433.sock"
#define SOCK_CONF_868 "/tmp/loraconf868.sock"

#define DEFAULT_SOCKET_TIMEOUT_MS 8000
#define DEFAULT_RSSI_TIMEOUT_MS   4000

static int g_ok = 0;
static int g_fail = 0;
static int g_skip = 0;

static pid_t g_daemon_pid = -1;

static const char *lora_cfg_433 =
    "SET MODE=LORA FREQ=433.775 SF=12 BW=125 CR=5 "
    "CRC=1 PREAMBLE=8 SYNC=0x12 LDRO=1 POWER=17\n";

static const char *lora_cfg_868 =
    "SET MODE=LORA FREQ=869.525 SF=11 BW=250 CR=5 "
    "CRC=1 PREAMBLE=16 SYNC=0x2B LDRO=AUTO POWER=10\n";

static const char *fsk_cfg_433 =
    "SET MODE=FSK FREQ=433.775 BR=9.6 FREQDEV=3.0 "
    "RXBW=20.8 SHAPING=0.5 ENCODING=2 "
    "PREAMBLE=32 SYNC=0x2DD4 POWER=10\n";

static const char *fsk_cfg_868 =
    "SET MODE=FSK FREQ=869.525 BR=9.6 FREQDEV=10.0 "
    "RXBW=25.0 SHAPING=0.5 ENCODING=2 "
    "PREAMBLE=32 SYNC=0x2DD4 POWER=10\n";

/* Kurze FSK-Configs fuer optionale TX-Tests */
static const char *fsk_fast_433 =
    "SET MODE=FSK FREQ=433.775 BR=100 FREQDEV=10.0 "
    "RXBW=250.0 SHAPING=0.5 ENCODING=2 "
    "PREAMBLE=32 SYNC=0x2DD4 POWER=0\n";

static const char *fsk_fast_868 =
    "SET MODE=FSK FREQ=869.525 BR=100 FREQDEV=10.0 "
    "RXBW=250.0 SHAPING=0.5 ENCODING=2 "
    "PREAMBLE=32 SYNC=0x2DD4 POWER=0\n";

static void fail_msg(const char *fmt, ...)
{
    va_list ap;

    printf("[FAIL] ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

static void info_msg(const char *fmt, ...)
{
    va_list ap;

    printf("[INFO] ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

static long now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)(ts.tv_sec * 1000L + ts.tv_nsec / 1000000L);
}

static int path_exists(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0;
}

static int connect_unix_once(const char *path)
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

static int connect_unix_retry(const char *path, int timeout_ms)
{
    long deadline = now_ms() + timeout_ms;
    int fd;

    while (now_ms() < deadline) {
        fd = connect_unix_once(path);
        if (fd >= 0)
            return fd;

        usleep(50000);
    }

    return -1;
}

static int wait_for_socket(const char *path, int timeout_ms)
{
    int fd;
    long deadline = now_ms() + timeout_ms;

    while (now_ms() < deadline) {
        if (path_exists(path)) {
            fd = connect_unix_retry(path, 300);
            if (fd >= 0) {
                close(fd);
                return 0;
            }
        }

        usleep(100000);
    }

    return -1;
}

static int write_all(int fd, const void *buf, size_t len)
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

static int send_conf_path(const char *path, const char *cmd)
{
    int fd = connect_unix_retry(path, 2000);

    if (fd < 0)
        return -1;

    if (write_all(fd, cmd, strlen(cmd)) < 0) {
        close(fd);
        return -1;
    }

    usleep(150000);
    close(fd);
    return 0;
}

static int send_conf_band(int band, const char *cmd)
{
    if (band == 433)
        return send_conf_path(SOCK_CONF_433, cmd);
    if (band == 868)
        return send_conf_path(SOCK_CONF_868, cmd);

    return -1;
}

static int line_matches_regex(const char *line, const char *pattern)
{
    regex_t rx;
    int ret;

    if (regcomp(&rx, pattern, REG_EXTENDED | REG_NOSUB) != 0)
        return 0;

    ret = regexec(&rx, line, 0, NULL, 0);
    regfree(&rx);

    return ret == 0;
}

static int wait_for_matching_line(int fd, const char *pattern,
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

        if (remaining < 1)
            remaining = 1;

        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int pr = poll(&pfd, 1, remaining);
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
                            strncpy(out, line, out_size - 1);
                            out[out_size - 1] = '\0';
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

static int assert_getrssi_stream(int band)
{
    const char *path = (band == 433) ? SOCK_CONF_433 : SOCK_CONF_868;
    int fd = connect_unix_retry(path, 2000);
    char line[128];

    if (fd < 0)
        return -1;

    if (write_all(fd, "SET GETRSSI=1\n", strlen("SET GETRSSI=1\n")) < 0) {
        close(fd);
        return -1;
    }

    if (wait_for_matching_line(fd,
                               "^RSSI=-?[0-9]+\\.[0-9][0-9]$",
                               DEFAULT_RSSI_TIMEOUT_MS,
                               line,
                               sizeof(line)) < 0) {
        close(fd);
        return -1;
    }

    printf("       %d RSSI line: %s\n", band, line);

    (void)write_all(fd, "SET GETRSSI=0\n", strlen("SET GETRSSI=0\n"));
    usleep(200000);

    close(fd);
    return 0;
}

static int test_all_sockets(void)
{
    const char *paths[] = {
        SOCK_DATA_433,
        SOCK_DATA_868,
        SOCK_CONF_433,
        SOCK_CONF_868
    };

    for (int i = 0; i < ARRAY_LEN(paths); i++) {
        int fd;

        if (!path_exists(paths[i])) {
            fail_msg("missing socket: %s", paths[i]);
            return -1;
        }

        fd = connect_unix_retry(paths[i], 2000);
        if (fd < 0) {
            fail_msg("cannot connect: %s", paths[i]);
            return -1;
        }

        close(fd);
    }

    return 0;
}

static int test_lora_433(void)
{
    if (send_conf_band(433, lora_cfg_433) < 0)
        return -1;

    return assert_getrssi_stream(433);
}

static int test_lora_868(void)
{
    if (send_conf_band(868, lora_cfg_868) < 0)
        return -1;

    return assert_getrssi_stream(868);
}

static int test_fsk_433(void)
{
    if (send_conf_band(433, fsk_cfg_433) < 0)
        return -1;

    return assert_getrssi_stream(433);
}

static int test_fsk_868(void)
{
    if (send_conf_band(868, fsk_cfg_868) < 0)
        return -1;

    return assert_getrssi_stream(868);
}

static int test_bad_configs_433(void)
{
    const char *bad[] = {
        "BOGUS\n",
        "SET MODE=NOPE\n",
        "SET GETRSSI=2\n",
        "SET MODE=LORA SF=6 BW=123 CRC=2 POWER=99\n",
        "SET MODE=FSK SF=12 BW=125 CR=5 LDRO=1\n"
    };

    for (int i = 0; i < ARRAY_LEN(bad); i++) {
        int fd;

        if (send_conf_band(433, bad[i]) < 0)
            return -1;

        fd = connect_unix_retry(SOCK_CONF_433, 1000);
        if (fd < 0)
            return -1;
        close(fd);
    }

    return 0;
}

static int test_bad_configs_868(void)
{
    const char *bad[] = {
        "BOGUS\n",
        "SET MODE=NOPE\n",
        "SET GETRSSI=2\n",
        "SET MODE=LORA SF=13 BW=200 CRC=2 POWER=-1\n",
        "SET MODE=FSK SF=11 BW=250 CR=5 LDRO=AUTO\n"
    };

    for (int i = 0; i < ARRAY_LEN(bad); i++) {
        int fd;

        if (send_conf_band(868, bad[i]) < 0)
            return -1;

        fd = connect_unix_retry(SOCK_CONF_868, 1000);
        if (fd < 0)
            return -1;
        close(fd);
    }

    return 0;
}

static int test_data_write_path_433(void)
{
    const uint8_t payload1[] = "loradaemon-iface-test-433";
    const uint8_t payload2[] = {
        0x00, 0x01, 0x02, 0x03,
        0x10, 0x20, 0x30, 0x40,
        0xAA, 0x55, 0xFF
    };
    int fd;

    if (send_conf_band(433, fsk_fast_433) < 0)
        return -1;

    fd = connect_unix_retry(SOCK_DATA_433, 2000);
    if (fd < 0)
        return -1;

    if (write_all(fd, payload1, sizeof(payload1) - 1) < 0) {
        close(fd);
        return -1;
    }

    usleep(250000);

    if (write_all(fd, payload2, sizeof(payload2)) < 0) {
        close(fd);
        return -1;
    }

    close(fd);
    usleep(250000);

    fd = connect_unix_retry(SOCK_DATA_433, 1000);
    if (fd < 0)
        return -1;

    close(fd);
    return 0;
}

static int test_data_write_path_868(void)
{
    const uint8_t payload1[] = "loradaemon-iface-test-868";
    const uint8_t payload2[] = {
        0x00, 0x01, 0x02, 0x03,
        0x10, 0x20, 0x30, 0x40,
        0xAA, 0x55, 0xFF
    };
    int fd;

    if (send_conf_band(868, fsk_fast_868) < 0)
        return -1;

    fd = connect_unix_retry(SOCK_DATA_868, 2000);
    if (fd < 0)
        return -1;

    if (write_all(fd, payload1, sizeof(payload1) - 1) < 0) {
        close(fd);
        return -1;
    }

    usleep(250000);

    if (write_all(fd, payload2, sizeof(payload2)) < 0) {
        close(fd);
        return -1;
    }

    close(fd);
    usleep(250000);

    fd = connect_unix_retry(SOCK_DATA_868, 1000);
    if (fd < 0)
        return -1;

    close(fd);
    return 0;
}

static int observe_rx(double seconds)
{
    int fd433 = -1;
    int fd868 = -1;
    long deadline;
    int seen = 0;

    fd433 = connect_unix_retry(SOCK_DATA_433, 2000);
    fd868 = connect_unix_retry(SOCK_DATA_868, 2000);

    if (fd433 < 0 || fd868 < 0) {
        if (fd433 >= 0)
            close(fd433);
        if (fd868 >= 0)
            close(fd868);
        return -1;
    }

    info_msg("observing RX for %.1f seconds", seconds);
    deadline = now_ms() + (long)(seconds * 1000.0);

    while (now_ms() < deadline) {
        struct pollfd pfds[2];
        int remaining = (int)(deadline - now_ms());

        if (remaining < 1)
            remaining = 1;
        if (remaining > 500)
            remaining = 500;

        pfds[0].fd = fd433;
        pfds[0].events = POLLIN;
        pfds[0].revents = 0;

        pfds[1].fd = fd868;
        pfds[1].events = POLLIN;
        pfds[1].revents = 0;

        int pr = poll(pfds, 2, remaining);
        if (pr < 0) {
            if (errno == EINTR)
                continue;
            close(fd433);
            close(fd868);
            return -1;
        }

        for (int idx = 0; idx < 2; idx++) {
            if (pfds[idx].revents & POLLIN) {
                uint8_t buf[4096];
                ssize_t n = read(pfds[idx].fd, buf, sizeof(buf));
                int band = (idx == 0) ? 433 : 868;

                if (n > 0) {
                    seen++;
                    printf("[RX %d] %zd bytes HEX=", band, n);

                    for (ssize_t i = 0; i < n && i < 64; i++)
                        printf("%02X ", buf[i]);

                    printf(" ASCII=");

                    for (ssize_t i = 0; i < n && i < 64; i++)
                        putchar((buf[i] >= 32 && buf[i] <= 126) ? buf[i] : '.');

                    printf("\n");
                }
            }
        }
    }

    if (seen == 0)
        info_msg("no RF packets observed; this is not a failure");

    close(fd433);
    close(fd868);
    return 0;
}

static int test_cli_invalid_option(const char *bin)
{
    pid_t pid;
    int pipefd[2];
    char out[1024];
    ssize_t n;
    int status;

    if (!bin)
        return 1; /* skip */

    if (pipe(pipefd) < 0)
        return -1;

    pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execl(bin, bin, "-x", (char *)NULL);
        _exit(127);
    }

    close(pipefd[1]);
    n = read(pipefd[0], out, sizeof(out) - 1);
    close(pipefd[0]);

    waitpid(pid, &status, 0);

    if (n < 0)
        return -1;

    out[n] = '\0';

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
        return -1;

    if (strstr(out, "Nutzung:") == NULL &&
        strstr(out, "invalid option") == NULL)
        return -1;

    return 0;
}

static int start_daemon(const char *bin)
{
    pid_t pid = fork();

    if (pid < 0)
        return -1;

    if (pid == 0) {
        setpgid(0, 0);
        execl(bin, bin, (char *)NULL);
        _exit(127);
    }

    g_daemon_pid = pid;
    return 0;
}

static void stop_daemon(void)
{
    int status;

    if (g_daemon_pid <= 0)
        return;

    kill(-g_daemon_pid, SIGTERM);

    for (int i = 0; i < 30; i++) {
        pid_t r = waitpid(g_daemon_pid, &status, WNOHANG);
        if (r == g_daemon_pid) {
            g_daemon_pid = -1;
            return;
        }
        usleep(100000);
    }

    kill(-g_daemon_pid, SIGKILL);
    waitpid(g_daemon_pid, &status, 0);
    g_daemon_pid = -1;
}

static int daemon_alive(void)
{
    if (g_daemon_pid <= 0)
        return 1;

    if (kill(g_daemon_pid, 0) == 0)
        return 1;

    return 0;
}

static int wait_all_sockets(int timeout_ms)
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
            return -1;
        }
    }

    return 0;
}

static void run_test(const char *name, int (*fn)(void))
{
    int ret;

    printf("[TEST] %s\n", name);
    ret = fn();

    if (ret == 0) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else if (ret == 1) {
        g_skip++;
        printf("[SKIP] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s\n", name);
    }
}

static int wrapper_cli_skip(void)
{
    return 1;
}

static int (*g_cli_test_fn)(void) = wrapper_cli_skip;
static const char *g_cli_bin = NULL;

static int wrapper_cli(void)
{
    return test_cli_invalid_option(g_cli_bin);
}

static bool g_rf_tx = false;
static double g_rx_seconds = 0.0;

static int wrapper_rf_433(void)
{
    if (!g_rf_tx)
        return 1;
    return test_data_write_path_433();
}

static int wrapper_rf_868(void)
{
    if (!g_rf_tx)
        return 1;
    return test_data_write_path_868();
}

static int wrapper_rx_observe(void)
{
    if (g_rx_seconds <= 0.0)
        return 1;
    return observe_rx(g_rx_seconds);
}

static void usage(const char *argv0)
{
    printf("Usage: %s [--bin ./loraham_daemon] [--rf-tx] [--rx-seconds N]\n", argv0);
}

int main(int argc, char **argv)
{
    const char *bin = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--bin") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return 2;
            }
            bin = argv[++i];
        } else if (strcmp(argv[i], "--rf-tx") == 0) {
            g_rf_tx = true;
        } else if (strcmp(argv[i], "--rx-seconds") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return 2;
            }
            g_rx_seconds = atof(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0 ||
                   strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (bin) {
        g_cli_bin = bin;
        g_cli_test_fn = wrapper_cli;

        info_msg("starting daemon: %s", bin);
        if (start_daemon(bin) < 0) {
            perror("start_daemon");
            return 1;
        }
    }

    run_test("CLI rejects invalid option", g_cli_test_fn);

    if (wait_all_sockets(DEFAULT_SOCKET_TIMEOUT_MS) < 0) {
        stop_daemon();
        return 1;
    }

    if (!daemon_alive()) {
        fail_msg("daemon exited early");
        stop_daemon();
        return 1;
    }

    run_test("all Unix sockets exist and connect", test_all_sockets);

    run_test("LoRa config 433 + RSSI", test_lora_433);
    run_test("LoRa config 868 + RSSI", test_lora_868);

    run_test("FSK config 433 + RSSI", test_fsk_433);
    run_test("FSK config 868 + RSSI", test_fsk_868);

    run_test("bad config robustness 433", test_bad_configs_433);
    run_test("bad config robustness 868", test_bad_configs_868);

    run_test("data socket RF write path 433", wrapper_rf_433);
    run_test("data socket RF write path 868", wrapper_rf_868);

    run_test("optional RX observation", wrapper_rx_observe);

    if (!daemon_alive()) {
        fail_msg("daemon exited during test");
        g_fail++;
    }

    stop_daemon();

    printf("\nSummary: ok=%d fail=%d skip=%d\n", g_ok, g_fail, g_skip);

    return g_fail ? 1 : 0;
}