#include "common_loradaemon_test.h"

/*
 * RSSI multi-client behavior.
 *
 * One CONF client enables GETRSSI. All connected CONF clients on the same
 * band should receive the RSSI stream.
 */

static const char *g_bin = NULL;

/* --- Multi-client RSSI broadcast --- */

static int test_rssi_broadcast_two_conf_clients_433(void)
{
    int fd1;
    int fd2;
    char line1[128];
    char line2[128];

    fd1 = connect_unix_retry(SOCK_CONF_433, 2000);
    fd2 = connect_unix_retry(SOCK_CONF_433, 2000);

    if (fd1 < 0 || fd2 < 0) {
        if (fd1 >= 0)
            close(fd1);
        if (fd2 >= 0)
            close(fd2);
        return TEST_FAIL;
    }

    if (write_all(fd1, "SET GETRSSI=1\n", strlen("SET GETRSSI=1\n")) < 0) {
        close(fd1);
        close(fd2);
        return TEST_FAIL;
    }

    if (wait_for_matching_line(fd1,
                               "^RSSI=-?[0-9]+\\.[0-9][0-9]$",
                               DEFAULT_RSSI_TIMEOUT_MS,
                               line1,
                               sizeof(line1)) < 0) {
        close(fd1);
        close(fd2);
        return TEST_FAIL;
    }

    if (wait_for_matching_line(fd2,
                               "^RSSI=-?[0-9]+\\.[0-9][0-9]$",
                               DEFAULT_RSSI_TIMEOUT_MS,
                               line2,
                               sizeof(line2)) < 0) {
        close(fd1);
        close(fd2);
        return TEST_FAIL;
    }

    printf("       client 1 RSSI line: %s\n", line1);
    printf("       client 2 RSSI line: %s\n", line2);

    (void)write_all(fd1, "SET GETRSSI=0\n", strlen("SET GETRSSI=0\n"));

    close(fd1);
    close(fd2);

    return TEST_PASS;
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

    run_test("RSSI broadcast to two CONF clients 433",
             test_rssi_broadcast_two_conf_clients_433);

    if (!daemon_alive()) {
        fail_msg("daemon exited during test");
        g_fail++;
    }

    stop_daemon();
    return print_summary();
}
