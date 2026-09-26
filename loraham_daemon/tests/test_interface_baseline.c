#include "common_loradaemon_test.h"

#include "../loraham_runtime.h"
#include <sys/file.h>

/*
 * Baseline interface test.
 *
 * This file checks the current public behavior before refactoring:
 * CLI handling, socket availability, LoRa/FSK config paths, RSSI streaming
 * and optional RF-TX smoke tests.
 */

/* --- CLI state --- */

static const char *g_bin = NULL;
static bool g_rf_tx = false;
static double g_rx_seconds = 0.0;

static int test_cli_wrapper(void)
{
    return test_cli_invalid_option(g_bin);
}



static int test_cli_debug_long(void)
{
    return test_cli_help_option(g_bin, "--debug");
}


static int test_cli_radio_help(void)
{
    char out[2048];
    int exit_code = 0;
    int ret = run_cli_capture(g_bin, "--radio=433", "--help",
                              out, sizeof(out), &exit_code);

    if (ret != TEST_PASS)
        return ret;

    if (exit_code != 0)
        return TEST_FAIL;

    if (strstr(out, "--radio MODE") == NULL)
        return TEST_FAIL;

    return TEST_PASS;
}

static int test_cli_radio_invalid(void)
{
    char out[2048];
    int exit_code = 0;
    int ret = run_cli_capture(g_bin, "--radio=915", NULL,
                              out, sizeof(out), &exit_code);

    if (ret != TEST_PASS)
        return ret;

    if (exit_code == 0)
        return TEST_FAIL;

    if (strstr(out, "invalid radio mode") == NULL)
        return TEST_FAIL;

    return TEST_PASS;
}

/* --radio is mandatory: a bare start must fail closed via the usage path
 * without creating any socket. */
static int test_cli_radio_required(void)
{
    char out[2048];
    int exit_code = 0;
    int ret = run_cli_capture(g_bin, NULL, NULL,
                              out, sizeof(out), &exit_code);

    if (ret != TEST_PASS)
        return ret;

    if (exit_code == 0)
        return TEST_FAIL;

    if (strstr(out, "missing option: --radio") == NULL)
        return TEST_FAIL;

    if (path_exists(SOCK_DATA_433) || path_exists(SOCK_DATA_868) ||
        path_exists(SOCK_CONF_433) || path_exists(SOCK_CONF_868)) {
        fail_msg("sockets created despite missing --radio");
        return TEST_FAIL;
    }

    return TEST_PASS;
}

/* --hw: unknown preset fails closed via the usage path; the default preset
 * name is accepted (help exits before hardware setup). */
static int test_cli_hw_unknown_rejected(void)
{
    char out[2048];
    int exit_code = 0;
    int ret = run_cli_capture(g_bin, "--radio=433", "--hw=bogus-board",
                              out, sizeof(out), &exit_code);

    if (ret != TEST_PASS)
        return ret;

    if (exit_code == 0)
        return TEST_FAIL;

    if (strstr(out, "invalid hardware profile") == NULL)
        return TEST_FAIL;

    return TEST_PASS;
}

static int test_cli_hw_loraham_accepted(void)
{
    char out[2048];
    int exit_code = 0;
    int ret = run_cli_capture(g_bin, "--hw=loraham", "--help",
                              out, sizeof(out), &exit_code);

    if (ret != TEST_PASS)
        return ret;
    if (exit_code != 0)
        return TEST_FAIL;
    if (strstr(out, "--hw PRESET") == NULL)
        return TEST_FAIL;
    return TEST_PASS;
}


/* --high-power is a bare flag: accepted alone (help exits before hardware
 * setup and lists it), refused with any value -- there is no `off` syntax,
 * absence is off. */
static int test_cli_high_power_bare_accepted(void)
{
    char out[4096];
    int exit_code = 0;
    int ret = run_cli_capture(g_bin, "--high-power", "--help",
                              out, sizeof(out), &exit_code);

    if (ret != TEST_PASS)
        return ret;
    if (exit_code != 0)
        return TEST_FAIL;
    if (strstr(out, "--high-power") == NULL)
        return TEST_FAIL;
    return TEST_PASS;
}

static int test_cli_high_power_value_rejected(void)
{
    static const char *flags[] = {
        "--high-power=off",
        "--high-power=on",
    };

    for (int i = 0; i < ARRAY_LEN(flags); i++) {
        char out[2048];
        int exit_code = 0;
        int ret = run_cli_capture(g_bin, "--radio=433", flags[i],
                                  out, sizeof(out), &exit_code);

        if (ret != TEST_PASS)
            return ret;

        if (exit_code == 0) {
            fail_msg("high-power value accepted: %s", flags[i]);
            return TEST_FAIL;
        }
    }

    return TEST_PASS;
}

/* The removed band-suffixed overrides must fail closed as unknown options. */
static int test_cli_banded_flag_rejected(void)
{
    static const char *flags[] = {
        "--tx-mode-433=direct",
        "--tx-mode-868=direct",
        "--cad-monitor-433=on",
        "--cad-monitor-868=on",
        "--cad-rssi-433=-90",
        "--cad-rssi-868=-90"
    };

    for (int i = 0; i < ARRAY_LEN(flags); i++) {
        char out[2048];
        int exit_code = 0;
        int ret = run_cli_capture(g_bin, flags[i], NULL,
                                  out, sizeof(out), &exit_code);

        if (ret != TEST_PASS)
            return ret;

        if (exit_code == 0) {
            fail_msg("banded flag accepted: %s", flags[i]);
            return TEST_FAIL;
        }

        if (strstr(out, "unrecognized option") == NULL &&
            strstr(out, "Usage:") == NULL) {
            fail_msg("no usage-error output for: %s", flags[i]);
            return TEST_FAIL;
        }
    }

    return TEST_PASS;
}

/* "both" is no longer a valid radio selection. */
static int test_cli_radio_both_rejected(void)
{
    char out[2048];
    int exit_code = 0;
    int ret = run_cli_capture(g_bin, "--radio=both", NULL,
                              out, sizeof(out), &exit_code);

    if (ret != TEST_PASS)
        return ret;

    if (exit_code == 0)
        return TEST_FAIL;

    if (strstr(out, "invalid radio mode") == NULL)
        return TEST_FAIL;

    return TEST_PASS;
}



static int start_daemon_radio(const char *radio)
{
    pid_t pid;

    /* Inherit the dev/test lock+socket dir overrides across execl. */
    ensure_test_runtime_dir();

    pid = fork();

    if (pid < 0)
        return TEST_FAIL;

    if (pid == 0) {
        setpgid(0, 0);
        execl(g_bin, g_bin, "--radio", radio, (char *)NULL);
        _exit(127);
    }

    (void)setpgid(pid, pid);

    g_daemon_pid = pid;
    return TEST_PASS;
}

static int wait_radio_sockets_433(void)
{
    if (wait_for_socket(SOCK_DATA_433, DEFAULT_SOCKET_TIMEOUT_MS) < 0)
        return TEST_FAIL;
    if (wait_for_socket(SOCK_CONF_433, DEFAULT_SOCKET_TIMEOUT_MS) < 0)
        return TEST_FAIL;

    if (path_exists(SOCK_DATA_868) || path_exists(SOCK_CONF_868)) {
        fail_msg("868 sockets unexpectedly exist in --radio 433 mode");
        return TEST_FAIL;
    }

    return TEST_PASS;
}

static int wait_radio_sockets_868(void)
{
    if (wait_for_socket(SOCK_DATA_868, DEFAULT_SOCKET_TIMEOUT_MS) < 0)
        return TEST_FAIL;
    if (wait_for_socket(SOCK_CONF_868, DEFAULT_SOCKET_TIMEOUT_MS) < 0)
        return TEST_FAIL;

    if (path_exists(SOCK_DATA_433) || path_exists(SOCK_CONF_433)) {
        fail_msg("433 sockets unexpectedly exist in --radio 868 mode");
        return TEST_FAIL;
    }

    return TEST_PASS;
}

static int test_single_radio_socket_mode_433(void)
{
    /* Exposing one band's sockets means staying up, which needs a ready radio. */
    if (radio_hardware_missing())
        return TEST_SKIP;

    if (start_daemon_radio("433") < 0)
        return TEST_FAIL;

    if (wait_radio_sockets_433() < 0) {
        stop_daemon();
        return TEST_FAIL;
    }

    stop_daemon();

    if (public_sockets_removed() < 0)
        return TEST_FAIL;

    return TEST_PASS;
}

static int test_single_radio_socket_mode_868(void)
{
    if (radio_hardware_missing())
        return TEST_SKIP;

    if (start_daemon_radio("868") < 0)
        return TEST_FAIL;

    if (wait_radio_sockets_868() < 0) {
        stop_daemon();
        return TEST_FAIL;
    }

    stop_daemon();

    if (public_sockets_removed() < 0)
        return TEST_FAIL;

    return TEST_PASS;
}


/* The SX1262 profile on a bench without the HAT: begin() fails with
 * CHIP_NOT_FOUND, the one-line D8 diagnosis is printed, and because no
 * selected radio becomes ready the daemon exits fail-closed without leaving
 * sockets behind. Exercises the Sx1262Driver construction and
 * begin()-failure path without hardware. */
/* Audit P1: startup lock-infrastructure failures must exit EXACTLY
 * LORAHAM_EXIT_LOCK_ERROR (4, restart-suppressed) — not the generic 1. */
static int run_daemon_in_runtime_dir_expect(const char *runtime_dir,
                                            int expected_code,
                                            const char *what)
{
    const char *saved = getenv("LORAHAM_RUNTIME_DIR");
    char saved_buf[256];
    pid_t pid;
    int status;
    int rc = TEST_FAIL;

    if (saved)
        snprintf(saved_buf, sizeof(saved_buf), "%s", saved);

    setenv("LORAHAM_RUNTIME_DIR", runtime_dir, 1);

    pid = fork();
    if (pid == 0) {
        setpgid(0, 0);
        execl(g_bin, g_bin, "--radio", "433", (char *)NULL);
        _exit(127);
    }
    if (pid > 0)
        (void)setpgid(pid, pid);

    if (pid > 0 && waitpid(pid, &status, 0) == pid &&
        WIFEXITED(status) && WEXITSTATUS(status) == expected_code) {
        rc = TEST_PASS;
    } else if (pid > 0) {
        fail_msg("%s: expected exit %d, got %d", what, expected_code,
                 WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    }

    if (saved)
        setenv("LORAHAM_RUNTIME_DIR", saved_buf, 1);
    else
        unsetenv("LORAHAM_RUNTIME_DIR");

    return rc;
}

static int test_gpio_lock_held_exits_lock_error(void)
{
    char dir[128];
    char lockpath[192];
    int fd;
    int rc;

    ensure_test_runtime_dir();
    snprintf(dir, sizeof(dir), "/tmp/loraham-exit4-gpio-%d", (int)getpid());
    mkdir(dir, 0700);

    /* Peer holds a legacy-433 profile pin lock (BCM 5 = RST). */
    snprintf(lockpath, sizeof(lockpath), "%s/gpio5.lock", dir);
    fd = open(lockpath, O_CREAT | O_RDWR, 0600);
    if (fd < 0 || flock(fd, LOCK_EX | LOCK_NB) != 0) {
        if (fd >= 0)
            close(fd);
        return TEST_FAIL;
    }

    rc = run_daemon_in_runtime_dir_expect(dir, LORAHAM_EXIT_LOCK_ERROR,
                                          "held gpio lock");
    flock(fd, LOCK_UN);
    close(fd);
    return rc;
}

static int test_spi_lock_unusable_exits_lock_error(void)
{
    char dir[128];
    char lockpath[192];

    /* Without an SPI device the band aborts before it ever takes the SPI lock,
     * so the daemon exits 1 and never reaches the LOCK_ERROR this asserts.  The
     * GPIO-lock twin above needs no such guard: that lock is taken first. */
    if (radio_hardware_missing())
        return TEST_SKIP;

    ensure_test_runtime_dir();
    snprintf(dir, sizeof(dir), "/tmp/loraham-exit4-spi-%d", (int)getpid());
    mkdir(dir, 0700);

    /* spi0.lock as a DIRECTORY: openat(O_CREAT) fails, lock unusable. */
    snprintf(lockpath, sizeof(lockpath), "%s/spi0.lock", dir);
    mkdir(lockpath, 0700);

    return run_daemon_in_runtime_dir_expect(dir, LORAHAM_EXIT_LOCK_ERROR,
                                            "unusable spi0.lock");
}

/*
 * Audit HW-2: a startup GPIO failure must exit LORAHAM_EXIT_LOCK_ERROR (4,
 * restart-suppressed), not the generic 1.
 *
 * gpiochip0 that cannot be opened -- wrong device, missing node, no permission
 * -- is not transient: every restart repeats it, and with exit 1 and
 * RestartSec=2 the unit spins forever. This ran as exit 1 before the repair.
 *
 * The condition is the host's, not the test's: where gpiochip0 IS usable the
 * daemon gets past LED setup and this proves nothing, so it skips rather than
 * pass for the wrong reason.
 */
static int test_gpio_unavailable_exits_lock_error(void)
{
    char dir[128];

    if (access("/dev/gpiochip0", F_OK) == 0)
        return TEST_SKIP;

    ensure_test_runtime_dir();
    snprintf(dir, sizeof(dir), "/tmp/loraham-exit4-gpio-%d", (int)getpid());
    mkdir(dir, 0700);

    return run_daemon_in_runtime_dir_expect(dir, LORAHAM_EXIT_LOCK_ERROR,
                                            "unopenable gpiochip0");
}

static int test_waveshare_profile_fails_closed(void)
{
    pid_t pid;

    ensure_test_runtime_dir();

    pid = fork();

    if (pid < 0)
        return TEST_FAIL;

    if (pid == 0) {
        setpgid(0, 0);
        execl(g_bin, g_bin, "--radio", "868", "--hw", "waveshare-sx1262",
              (char *)NULL);
        _exit(127);
    }
    if (pid > 0)
        (void)setpgid(pid, pid);

    g_daemon_pid = pid;

    /* Fail-closed startup: no ready radio -> process exits on its own. */
    if (wait_owned_daemon_exit(15000) != TEST_PASS) {
        fail_msg("daemon kept running despite missing SX1262");
        stop_daemon();
        return TEST_FAIL;
    }

    if (public_sockets_removed() < 0)
        return TEST_FAIL;

    return TEST_PASS;
}

/* --- Socket availability --- */

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
            return TEST_FAIL;
        }

        fd = connect_unix_retry(paths[i], 2000);
        if (fd < 0) {
            fail_msg("cannot connect: %s", paths[i]);
            return TEST_FAIL;
        }

        close(fd);
    }

    return TEST_PASS;
}

/* --- Normal config smoke tests --- */

static int test_lora_433(void)
{
    if (send_conf_band(433, lora_cfg_433) < 0)
        return TEST_FAIL;

    return assert_getrssi_stream(433);
}

static int test_lora_868(void)
{
    if (send_conf_band(868, lora_cfg_868) < 0)
        return TEST_FAIL;

    return assert_getrssi_stream(868);
}

static int test_fsk_433(void)
{
    if (send_conf_band(433, fsk_cfg_433) < 0)
        return TEST_FAIL;

    return assert_getrssi_stream(433);
}

static int test_fsk_868(void)
{
    if (send_conf_band(868, fsk_cfg_868) < 0)
        return TEST_FAIL;

    return assert_getrssi_stream(868);
}

/* --- Bad config smoke tests: must not kill the daemon --- */

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
            return TEST_FAIL;

        /* A fresh connection proves the daemon still accepts clients. */
        fd = connect_unix_retry(SOCK_CONF_433, 1000);
        if (fd < 0)
            return TEST_FAIL;

        close(fd);
    }

    return TEST_PASS;
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
            return TEST_FAIL;

        /* A fresh connection proves the daemon still accepts clients. */
        fd = connect_unix_retry(SOCK_CONF_868, 1000);
        if (fd < 0)
            return TEST_FAIL;

        close(fd);
    }

    return TEST_PASS;
}

/* --- Optional RF-TX path, enabled only by run_tests.sh --TX --- */

static int test_data_write_path_433(void)
{
    const uint8_t payload1[] = "loradaemon-iface-test-433";
    const uint8_t payload2[] = {
        0x00, 0x01, 0x02, 0x03,
        0x10, 0x20, 0x30, 0x40,
        0xAA, 0x55, 0xFF
    };
    int fd;

    if (!g_rf_tx)
        return TEST_SKIP;

    if (send_conf_band(433, fsk_fast_433) < 0)
        return TEST_FAIL;

    fd = connect_unix_retry(SOCK_DATA_433, 2000);
    if (fd < 0)
        return TEST_FAIL;

    if (write_all(fd, payload1, sizeof(payload1) - 1) < 0) {
        close(fd);
        return TEST_FAIL;
    }

    usleep(250000);

    if (write_all(fd, payload2, sizeof(payload2)) < 0) {
        close(fd);
        return TEST_FAIL;
    }

    close(fd);
    usleep(250000);

    /* Reconnect checks that TX did not break the data socket. */
    fd = connect_unix_retry(SOCK_DATA_433, 1000);
    if (fd < 0)
        return TEST_FAIL;

    close(fd);
    return TEST_PASS;
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

    if (!g_rf_tx)
        return TEST_SKIP;

    if (send_conf_band(868, fsk_fast_868) < 0)
        return TEST_FAIL;

    fd = connect_unix_retry(SOCK_DATA_868, 2000);
    if (fd < 0)
        return TEST_FAIL;

    if (write_all(fd, payload1, sizeof(payload1) - 1) < 0) {
        close(fd);
        return TEST_FAIL;
    }

    usleep(250000);

    if (write_all(fd, payload2, sizeof(payload2)) < 0) {
        close(fd);
        return TEST_FAIL;
    }

    close(fd);
    usleep(250000);

    /* Reconnect checks that TX did not break the data socket. */
    fd = connect_unix_retry(SOCK_DATA_868, 1000);
    if (fd < 0)
        return TEST_FAIL;

    close(fd);
    return TEST_PASS;
}

/* --- Optional RX observation for manual RF checks --- */

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
        return TEST_FAIL;
    }

    info_msg("observing RX for %.1f seconds", seconds);
    deadline = now_ms() + (long)(seconds * 1000.0);

    while (now_ms() < deadline) {
        struct pollfd pfds[2];
        int remaining = (int)(deadline - now_ms());
        int pr;

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

        pr = poll(pfds, 2, remaining);
        if (pr < 0) {
            if (errno == EINTR)
                continue;

            close(fd433);
            close(fd868);
            return TEST_FAIL;
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
    return TEST_PASS;
}

static int test_optional_rx_observe(void)
{
    if (!g_rf_tx || g_rx_seconds <= 0.0)
        return TEST_SKIP;

    return observe_rx(g_rx_seconds);
}

/* --- CLI parsing and test sequence --- */

static void usage(const char *argv0)
{
    printf("Usage: %s --bin ./loraham_daemon [--rf-tx] [--rx-seconds N]\n", argv0);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--bin") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return 2;
            }
            g_bin = argv[++i];
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

    if (!g_bin) {
        usage(argv[0]);
        return 2;
    }

    run_test("CLI rejects invalid option", test_cli_wrapper);
    run_test("CLI accepts --debug", test_cli_debug_long);
    run_test("CLI accepts --radio help", test_cli_radio_help);
    run_test("CLI rejects invalid --radio", test_cli_radio_invalid);
    run_test("CLI requires --radio", test_cli_radio_required);
    run_test("CLI rejects --radio both", test_cli_radio_both_rejected);
    run_test("CLI rejects banded flags", test_cli_banded_flag_rejected);
    run_test("CLI rejects unknown --hw preset", test_cli_hw_unknown_rejected);
    run_test("CLI accepts --hw loraham", test_cli_hw_loraham_accepted);
    run_test("CLI accepts bare --high-power", test_cli_high_power_bare_accepted);
    run_test("CLI rejects --high-power with a value", test_cli_high_power_value_rejected);

    run_test("single-radio socket mode 433", test_single_radio_socket_mode_433);
    run_test("single-radio socket mode 868", test_single_radio_socket_mode_868);
    run_test("waveshare profile fails closed without HAT",
             test_waveshare_profile_fails_closed);
    run_test("held GPIO lock exits LOCK_ERROR",
             test_gpio_lock_held_exits_lock_error);
    run_test("unusable spi0.lock exits LOCK_ERROR",
             test_spi_lock_unusable_exits_lock_error);
    run_test("unavailable gpiochip0 exits LOCK_ERROR",
             test_gpio_unavailable_exits_lock_error);

    if (radio_hardware_missing())
        return skip_live_daemon_tests();

    info_msg("starting daemons: %s (433 + 868)", g_bin);
    if (start_daemon(g_bin) < 0)
        return 1;

    if (start_daemon_868(g_bin) < 0) {
        stop_daemon();
        return 1;
    }

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

    run_test("data socket RF write path 433", test_data_write_path_433);
    run_test("data socket RF write path 868", test_data_write_path_868);

    run_test("optional RX observation", test_optional_rx_observe);

    if (!daemon_alive()) {
        fail_msg("daemon exited during test");
        g_fail++;
    }

    stop_daemon();
    return print_summary();
}
