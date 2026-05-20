#include "common_loradaemon_test.h"

/*
 * Config-stream behavior tests.
 *
 * These tests keep config sockets open across multiple commands and check
 * RSSI stream lifecycle behavior before the event-loop refactor.
 */

static const char *g_bin = NULL;

/* --- Persistent connection: several commands on one conf socket --- */

static int test_persistent_conf_commands_433(void)
{
    int fd = connect_unix_retry(SOCK_CONF_433, 2000);
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

    printf("       433 RSSI line: %s\n", line);

    if (write_all(fd, "SET GETRSSI=0\n", strlen("SET GETRSSI=0\n")) < 0) {
        close(fd);
        return TEST_FAIL;
    }

    usleep(200000);

    /* Same socket should still accept a normal LoRa config. */
    if (write_all(fd, lora_cfg_433, strlen(lora_cfg_433)) < 0) {
        close(fd);
        return TEST_FAIL;
    }

    usleep(200000);
    close(fd);

    return daemon_alive() ? TEST_PASS : TEST_FAIL;
}

/* --- RSSI stream should be usable again after client disconnect --- */

static int test_rssi_reconnect_433(void)
{
    int fd;

    fd = connect_unix_retry(SOCK_CONF_433, 2000);
    if (fd < 0)
        return TEST_FAIL;

    if (write_all(fd, "SET GETRSSI=1\n", strlen("SET GETRSSI=1\n")) < 0) {
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

    /* Close without explicit GETRSSI=0 to test daemon-side cleanup. */
    close(fd);
    usleep(500000);

    fd = connect_unix_retry(SOCK_CONF_433, 2000);
    if (fd < 0)
        return TEST_FAIL;

    if (write_all(fd, "SET GETRSSI=1\n", strlen("SET GETRSSI=1\n")) < 0) {
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

/* --- Both bands should stream RSSI independently --- */

static int test_parallel_rssi_433_868(void)
{
    int fd433;
    int fd868;

    fd433 = connect_unix_retry(SOCK_CONF_433, 2000);
    fd868 = connect_unix_retry(SOCK_CONF_868, 2000);

    if (fd433 < 0 || fd868 < 0) {
        if (fd433 >= 0)
            close(fd433);
        if (fd868 >= 0)
            close(fd868);
        return TEST_FAIL;
    }

    if (write_all(fd433, "SET GETRSSI=1\n", strlen("SET GETRSSI=1\n")) < 0 ||
        write_all(fd868, "SET GETRSSI=1\n", strlen("SET GETRSSI=1\n")) < 0) {
        close(fd433);
        close(fd868);
        return TEST_FAIL;
    }

    if (wait_for_matching_line(fd433,
                               "^RSSI=-?[0-9]+\\.[0-9][0-9]$",
                               DEFAULT_RSSI_TIMEOUT_MS,
                               NULL,
                               0) < 0) {
        close(fd433);
        close(fd868);
        return TEST_FAIL;
    }

    if (wait_for_matching_line(fd868,
                               "^RSSI=-?[0-9]+\\.[0-9][0-9]$",
                               DEFAULT_RSSI_TIMEOUT_MS,
                               NULL,
                               0) < 0) {
        close(fd433);
        close(fd868);
        return TEST_FAIL;
    }

    (void)write_all(fd433, "SET GETRSSI=0\n", strlen("SET GETRSSI=0\n"));
    (void)write_all(fd868, "SET GETRSSI=0\n", strlen("SET GETRSSI=0\n"));

    close(fd433);
    close(fd868);

    return TEST_PASS;
}


/* --- CONFIG line framing ------------------------------------------------- */

static int test_fragmented_conf_command_433(void)
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

static int test_multiple_conf_commands_one_write_433(void)
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

static int test_final_unterminated_conf_command_on_close(void)
{
    int fd = connect_unix_retry(SOCK_CONF_433, 2000);

    if (fd < 0)
        return TEST_FAIL;

    if (write_all(fd, "SET POWER=10", strlen("SET POWER=10")) < 0) {
        close(fd);
        return TEST_FAIL;
    }

    close(fd);
    usleep(300000);

    return daemon_alive() ? TEST_PASS : TEST_FAIL;
}

/* --- CLI parsing and test sequence --- */

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

    run_test("persistent conf connection 433", test_persistent_conf_commands_433);
    run_test("fragmented conf command 433", test_fragmented_conf_command_433);
    run_test("multiple conf commands one write 433", test_multiple_conf_commands_one_write_433);
    run_test("final unterminated conf command on close", test_final_unterminated_conf_command_on_close);
    run_test("RSSI reconnect 433", test_rssi_reconnect_433);
    run_test("parallel RSSI 433 + 868", test_parallel_rssi_433_868);

    if (!daemon_alive()) {
        fail_msg("daemon exited during test");
        g_fail++;
    }

    stop_daemon();
    return print_summary();
}
