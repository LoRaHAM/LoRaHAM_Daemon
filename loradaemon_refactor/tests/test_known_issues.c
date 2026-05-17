#include "common_loradaemon_test.h"

/*
 * Known-issue tests.
 *
 * These describe desired future behavior. They are run as XFAIL so they
 * document bugfix targets without breaking the current baseline.
 */

static const char *g_bin = NULL;


/* --- Desired: command parser should buffer fragmented stream input --- */

static int desired_fragmented_conf_command_433(void)
{
    int fd = connect_unix_retry(SOCK_CONF_433, 2000);

    if (fd < 0)
        return TEST_FAIL;

    if (write_all(fd, "SET GETRSSI=", strlen("SET GETRSSI=")) < 0) {
        close(fd);
        return TEST_FAIL;
    }

    usleep(250000);

    if (write_all(fd, "1\n", strlen("1\n")) < 0) {
        close(fd);
        return TEST_FAIL;
    }

    if (wait_for_matching_line(fd,
                               "^RSSI=-?[0-9]+\\.[0-9][0-9]$",
                               DEFAULT_RSSI_TIMEOUT_MS,
                               NULL,
                               0) < 0) {
        close(fd);
        return TEST_FAIL;
    }

    (void)write_all(fd, "SET GETRSSI=0\n", strlen("SET GETRSSI=0\n"));
    close(fd);

    return TEST_PASS;
}

/* --- Desired: multiple newline-separated commands in one write --- */

static int desired_multiple_commands_one_write_433(void)
{
    int fd = connect_unix_retry(SOCK_CONF_433, 2000);
    const char *cmd = "BOGUS\nSET GETRSSI=1\n";

    if (fd < 0)
        return TEST_FAIL;

    if (write_all(fd, cmd, strlen(cmd)) < 0) {
        close(fd);
        return TEST_FAIL;
    }

    if (wait_for_matching_line(fd,
                               "^RSSI=-?[0-9]+\\.[0-9][0-9]$",
                               DEFAULT_RSSI_TIMEOUT_MS,
                               NULL,
                               0) < 0) {
        close(fd);
        return TEST_FAIL;
    }

    (void)write_all(fd, "SET GETRSSI=0\n", strlen("SET GETRSSI=0\n"));
    close(fd);

    return TEST_PASS;
}

/* --- Desired: parser should expose invalid numeric values as errors --- */

static int desired_strict_numeric_validation(void)
{
    /*
     * Current daemon has no stable OK/ERR response contract for invalid
     * numeric values like POWER=abc, CRC=abc, SF=12xyz, BW=125foo.
     */
    return TEST_FAIL;
}

/* --- CLI parsing and XFAIL sequence --- */

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--bin") == 0) {
            if (i + 1 >= argc) {
                usage_common(argv[0]);
                return 2;
            }
            g_bin = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 ||
                   strcmp(argv[i], "-h") == 0) {
            usage_common(argv[0]);
            return 0;
        } else {
            usage_common(argv[0]);
            return 2;
        }
    }

    if (!g_bin) {
        usage_common(argv[0]);
        return 2;
    }

    info_msg("starting daemon: %s", g_bin);
    if (start_daemon(g_bin) < 0)
        return 1;

    if (wait_all_sockets(DEFAULT_SOCKET_TIMEOUT_MS) < 0) {
        stop_daemon();
        return 1;
    }

    run_xfail_test("desired: fragmented config command is line-buffered",
                   desired_fragmented_conf_command_433);
    run_xfail_test("desired: multiple commands in one write are processed",
                   desired_multiple_commands_one_write_433);
    run_xfail_test("desired: strict numeric validation has observable errors",
                   desired_strict_numeric_validation);

    if (!daemon_alive()) {
        fail_msg("daemon exited during known-issue tests");
        g_fail++;
    }

    stop_daemon();
    return print_summary();
}
