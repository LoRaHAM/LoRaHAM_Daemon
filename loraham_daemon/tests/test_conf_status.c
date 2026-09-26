#include "common_loradaemon_test.h"

/*
 * CONF status query behavior.
 *
 * GET STATUS returns one snapshot line for the selected radio. Status/config
 * remains on the CONF socket; DATA sockets stay payload-only.
 */

static const char *g_bin = NULL;

static int start_daemon_433(const char *bin)
{
    pid_t pid;

    /* Inherit the dev/test lock+socket dir overrides across execl. */
    ensure_test_runtime_dir();

    pid = fork();

    if (pid < 0)
        return TEST_FAIL;

    if (pid == 0) {
        setpgid(0, 0);
        execl(bin, bin, "--radio", "433", (char *)NULL);
        _exit(127);
    }

    (void)setpgid(pid, pid);

    g_daemon_pid = pid;
    return TEST_PASS;
}

static int test_get_status_433(void)
{
    int fd;
    char line[512];

    fd = connect_unix_retry(SOCK_CONF_433, 2000);
    if (fd < 0)
        return TEST_FAIL;

    if (write_all(fd, "GET STATUS\n", strlen("GET STATUS\n")) < 0) {
        close(fd);
        return TEST_FAIL;
    }

    if (wait_for_matching_line(fd,
                               "^STATUS RADIO=(READY|FAILED|UNINITIALIZED) TX=[01] CAD=[01] GETRSSI=[01] TXRESULT=[01] TXMODE=(MANAGED|DIRECT) TXQUEUE=[01] TXQ=[0-9]+ TXQDROP=[0-9]+ TXQREJECT=[0-9]+ TXQSTALE=[0-9]+ TXQRESULTDROP=[0-9]+ TXQDONE=[0-9]+ TXQLAST=[A-Z_]+ TXQSEQ=[0-9]+ CADWAIT=[0-9]+ CADIDLE=[0-9]+ CADPOLL=[0-9]+ CADTXAFTERTIMEOUT=[01] CADMONITOR=[01] CADRSSI=-?[0-9]+ RXREADY=[01] HIGHPOWER=[01] CHIPFAMILY=(SX127x|SX1262)$",
                               2000,
                               line,
                               sizeof(line)) < 0) {
        close(fd);
        return TEST_FAIL;
    }

    printf("       433 status line: %s\n", line);

    close(fd);
    return TEST_PASS;
}


static int test_set_txresult_433(void)
{
    int fd;
    char line[512];

    fd = connect_unix_retry(SOCK_CONF_433, 2000);
    if (fd < 0)
        return TEST_FAIL;

    if (write_all(fd, "SET TXRESULT=1\nGET STATUS\n",
                  strlen("SET TXRESULT=1\nGET STATUS\n")) < 0) {
        close(fd);
        return TEST_FAIL;
    }

    if (wait_for_matching_line(fd,
                               "^STATUS RADIO=(READY|FAILED|UNINITIALIZED) TX=[01] CAD=[01] GETRSSI=[01] TXRESULT=1 TXMODE=(MANAGED|DIRECT) TXQUEUE=[01] TXQ=[0-9]+ TXQDROP=[0-9]+ TXQREJECT=[0-9]+ TXQSTALE=[0-9]+ TXQRESULTDROP=[0-9]+ TXQDONE=[0-9]+ TXQLAST=[A-Z_]+ TXQSEQ=[0-9]+ CADWAIT=[0-9]+ CADIDLE=[0-9]+ CADPOLL=[0-9]+ CADTXAFTERTIMEOUT=[01] CADMONITOR=[01] CADRSSI=-?[0-9]+ RXREADY=[01] HIGHPOWER=[01] CHIPFAMILY=(SX127x|SX1262)$",
                               2000,
                               line,
                               sizeof(line)) < 0) {
        close(fd);
        return TEST_FAIL;
    }

    printf("       433 txresult status line: %s\n", line);

    close(fd);
    return TEST_PASS;
}



static int test_set_txqueue_433(void)
{
    int fd;
    char line[512];

    fd = connect_unix_retry(SOCK_CONF_433, 2000);
    if (fd < 0)
        return TEST_FAIL;

    if (write_all(fd, "SET TXQUEUE=1\nGET STATUS\n",
                  strlen("SET TXQUEUE=1\nGET STATUS\n")) < 0) {
        close(fd);
        return TEST_FAIL;
    }

    if (wait_for_matching_line(fd,
                               "^STATUS RADIO=(READY|FAILED|UNINITIALIZED) TX=[01] CAD=[01] GETRSSI=[01] TXRESULT=[01] TXMODE=(MANAGED|DIRECT) TXQUEUE=1 TXQ=[0-9]+ TXQDROP=[0-9]+ TXQREJECT=[0-9]+ TXQSTALE=[0-9]+ TXQRESULTDROP=[0-9]+ TXQDONE=[0-9]+ TXQLAST=[A-Z_]+ TXQSEQ=[0-9]+ CADWAIT=[0-9]+ CADIDLE=[0-9]+ CADPOLL=[0-9]+ CADTXAFTERTIMEOUT=[01] CADMONITOR=[01] CADRSSI=-?[0-9]+ RXREADY=[01] HIGHPOWER=[01] CHIPFAMILY=(SX127x|SX1262)$",
                               2000,
                               line,
                               sizeof(line)) < 0) {
        close(fd);
        return TEST_FAIL;
    }

    printf("       433 txqueue status line: %s\n", line);

    close(fd);
    return TEST_PASS;
}

static int test_set_txmode_433(void)
{
    int fd;
    char line[512];

    fd = connect_unix_retry(SOCK_CONF_433, 2000);
    if (fd < 0)
        return TEST_FAIL;

    if (write_all(fd, "SET TXMODE=DIRECT\nGET STATUS\n",
                  strlen("SET TXMODE=DIRECT\nGET STATUS\n")) < 0) {
        close(fd);
        return TEST_FAIL;
    }

    if (wait_for_matching_line(fd,
                               "^STATUS RADIO=(READY|FAILED|UNINITIALIZED) TX=[01] CAD=[01] GETRSSI=[01] TXRESULT=[01] TXMODE=DIRECT TXQUEUE=[01] TXQ=[0-9]+ TXQDROP=[0-9]+ TXQREJECT=[0-9]+ TXQSTALE=[0-9]+ TXQRESULTDROP=[0-9]+ TXQDONE=[0-9]+ TXQLAST=[A-Z_]+ TXQSEQ=[0-9]+ CADWAIT=[0-9]+ CADIDLE=[0-9]+ CADPOLL=[0-9]+ CADTXAFTERTIMEOUT=[01] CADMONITOR=[01] CADRSSI=-?[0-9]+ RXREADY=[01] HIGHPOWER=[01] CHIPFAMILY=(SX127x|SX1262)$",
                               2000,
                               line,
                               sizeof(line)) < 0) {
        close(fd);
        return TEST_FAIL;
    }

    printf("       433 txmode status line: %s\n", line);

    close(fd);
    return TEST_PASS;
}

static int test_get_channel_433(void)
{
    int fd;
    char line[192];

    fd = connect_unix_retry(SOCK_CONF_433, 2000);
    if (fd < 0)
        return TEST_FAIL;

    if (write_all(fd, "GET CHANNEL\n", strlen("GET CHANNEL\n")) < 0) {
        close(fd);
        return TEST_FAIL;
    }

    if (wait_for_matching_line(fd,
                               "^CHANNEL RADIO=(READY|FAILED|UNINITIALIZED) BUSY=[01] CAD=[01] CADSCAN=[01] CADSTATE=(FREE|BUSY|UNAVAILABLE) RSSI=-?[0-9]+\\.[0-9][0-9] PACKETRSSI=-?[0-9]+\\.[0-9][0-9] LIVERSSI=-?[0-9]+\\.[0-9][0-9] MODE=(LORA|FSK) TXMODE=(MANAGED|DIRECT)$",
                               2000,
                               line,
                               sizeof(line)) < 0) {
        close(fd);
        return TEST_FAIL;
    }

    printf("       433 channel line: %s\n", line);

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

    info_msg("starting daemon: %s --radio 433", g_bin);
    if (start_daemon_433(g_bin) < 0)
        return 1;

    if (wait_for_socket(SOCK_CONF_433, DEFAULT_SOCKET_TIMEOUT_MS) < 0) {
        stop_daemon();
        return 1;
    }

    run_test("GET STATUS on CONF 433", test_get_status_433);
    run_test("SET TXRESULT on CONF 433", test_set_txresult_433);
    run_test("SET TXMODE on CONF 433", test_set_txmode_433);
    run_test("SET TXQUEUE on CONF 433", test_set_txqueue_433);
    run_test("GET CHANNEL on CONF 433", test_get_channel_433);

    if (!daemon_alive()) {
        fail_msg("daemon exited during test");
        g_fail++;
    }

    stop_daemon();
    return print_summary();
}
