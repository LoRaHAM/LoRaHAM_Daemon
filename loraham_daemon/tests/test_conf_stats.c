#include "common_loradaemon_test.h"

/*
 * CONF GET STATS behavior.
 *
 * This tests the external line-based CONF query interface.
 */

static const char *g_bin = NULL;

static int test_get_stats_433(void)
{
    int fd;
    char line[256];

    fd = connect_unix_retry(SOCK_CONF_433, 2000);
    if (fd < 0)
        return TEST_FAIL;

    if (write_all(fd, "GET STATS\n", strlen("GET STATS\n")) < 0) {
        close(fd);
        return TEST_FAIL;
    }

    if (wait_for_matching_line(
            fd,
            "^STATS UPTIME=[0-9]+ RADIO=READY RX=[0-9]+ RXBYTES=[0-9]+ RXDROPS=[0-9]+ TXOK=[0-9]+ TXERR=[0-9]+ TXBUSY=[0-9]+ CADTIMEOUT=[0-9]+ CADSEND=[0-9]+ RXREARMFAIL=[0-9]+$",
            DEFAULT_RSSI_TIMEOUT_MS,
            line,
            sizeof(line)) < 0) {
        close(fd);
        return TEST_FAIL;
    }

    printf("       433 stats line: %s\n", line);
    close(fd);
    return TEST_PASS;
}

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

    if (radio_hardware_missing())
        return skip_live_daemon_tests();

    info_msg("starting daemon: %s", g_bin);
    if (start_daemon(g_bin) < 0)
        return 1;

    if (wait_for_socket(SOCK_CONF_433, DEFAULT_SOCKET_TIMEOUT_MS) < 0) {
        fail_msg("socket not ready: %s", SOCK_CONF_433);
        stop_daemon();
        return 1;
    }

    run_test("GET STATS 433", test_get_stats_433);

    if (!daemon_alive()) {
        fail_msg("daemon exited during test");
        g_fail++;
    }

    stop_daemon();
    return print_summary();
}
