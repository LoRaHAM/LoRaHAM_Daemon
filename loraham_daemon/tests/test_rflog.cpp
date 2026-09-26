#include "../daemon_rflog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

/* --- RF log: line format, switch/path parsing, copy-truncate retention ---- */

static int g_ok = 0;
static int g_fail = 0;

static void expect_int(const char *name, long actual, long expected)
{
    if (actual == expected) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: expected %ld, got %ld\n", name, expected, actual);
    }
}

static void expect_str(const char *name, const char *actual, const char *expected)
{
    if (actual && strcmp(actual, expected) == 0) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s:\n  expected %s\n  got      %s\n", name, expected, actual ? actual : "(null)");
    }
}

static long file_size(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 ? (long)st.st_size : -1;
}

static long file_inode(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 ? (long)st.st_ino : -1;
}

static long count_lines(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    long n = 0;
    int c;
    while ((c = fgetc(f)) != EOF)
        if (c == '\n')
            n++;
    fclose(f);
    return n;
}

/* --- parsing -------------------------------------------------------------- */
static void test_switch_parse(void)
{
    bool v = false;
    expect_int("parse on ok", daemon_parse_rflog_switch("on", &v), 1);
    expect_int("parse on value", v, 1);
    expect_int("parse OFF ok (case-insensitive)", daemon_parse_rflog_switch("OFF", &v), 1);
    expect_int("parse OFF value", v, 0);
    expect_int("parse junk fails", daemon_parse_rflog_switch("yes", &v), 0);
    expect_int("parse empty fails", daemon_parse_rflog_switch("", &v), 0);
    expect_int("parse null fails", daemon_parse_rflog_switch(NULL, &v), 0);
}

static void test_path_must_be_absolute(void)
{
    daemon_rflog_reset();
    expect_int("relative path refused", daemon_set_rflog_path_global("logs/rf.log"), 0);
    expect_int("empty path refused", daemon_set_rflog_path_global(""), 0);
    expect_int("null path refused", daemon_set_rflog_path_global(NULL), 0);
    expect_int("absolute path accepted", daemon_set_rflog_path_global("/tmp/x.log"), 1);
}

static void test_on_without_path_refuses_start(void)
{
    daemon_rflog_reset();
    char err[256] = {0};
    expect_int("switch on set", daemon_set_rflog_switch_global("on"), 1);
    expect_int("start refused without a path", daemon_rflog_start(err, sizeof(err)), 0);
    expect_int("refusal names --rflog-path", strstr(err, "--rflog-path") != NULL, 1);
    expect_int("not active after refusal", daemon_rflog_active(), 0);
}

static void test_off_is_a_noop(void)
{
    daemon_rflog_reset();
    char err[256] = {0};
    expect_int("switch off set", daemon_set_rflog_switch_global("off"), 1);
    expect_int("start with off succeeds", daemon_rflog_start(err, sizeof(err)), 1);
    expect_int("not active when off", daemon_rflog_active(), 0);
}

/* --- the line contract ---------------------------------------------------- */
static void test_line_format(void)
{
    char line[512];
    const uint8_t payload[] = {0x3c, 0xff, 0x01, 'A', 'B', '"', '\\', 0x7f};

    size_t n = daemon_rflog_format_rx(line, sizeof(line), "2026-09-12T16:03:47.412Z",
                                      "433", -10450, 725, payload, sizeof(payload));
    expect_str("RX line",
               line,
               "2026-09-12T16:03:47.412Z RX rssi=-104.50 snr=7.25 len=8 band=433"
               " hex=3cff014142225c7f ascii=\"<..AB...\"\n");
    (void)n;

    n = daemon_rflog_format_tx(line, sizeof(line), "2026-09-12T16:03:51.006Z",
                               "868", "ok", payload, 3);
    expect_str("TX line carries no signal strength, but an outcome",
               line,
               "2026-09-12T16:03:51.006Z TX rssi=- snr=- len=3 outcome=ok band=868"
               " hex=3cff01 ascii=\"<..\"\n");
    expect_int("returned length equals strlen", (long)n, (long)strlen(line));
}

static void test_line_is_bounded(void)
{
    /* A 255-byte payload must never overrun a small buffer, and the record
     * must still end in a newline so the file stays one record per line. */
    uint8_t payload[255];
    memset(payload, 0x41, sizeof(payload));
    char line[96];
    size_t n = daemon_rflog_format_rx(line, sizeof(line), "t", "433", 0, 0, payload, sizeof(payload));
    expect_int("bounded length", (long)n, (long)sizeof(line) - 1);
    expect_int("bounded line still ends with newline", line[n - 1] == '\n', 1);
    expect_int("bounded line is NUL-terminated", line[n] == 0, 1);
}

/* --- retention: copy-truncate at the cap, same inode ---------------------- */
static void test_rollover_keeps_inode_and_previous_segment(void)
{
    char dir[] = "/tmp/rflog-test-XXXXXX";
    if (!mkdtemp(dir)) {
        expect_int("mkdtemp", 0, 1);
        return;
    }
    char path[128], prev[136];
    snprintf(path, sizeof(path), "%s/rf-daemon-433.log", dir);
    snprintf(prev, sizeof(prev), "%s.1", path);

    daemon_rflog_reset();
    daemon_rflog_set_max_bytes(2000);
    char err[256] = {0};
    expect_int("switch on", daemon_set_rflog_switch_global("on"), 1);
    expect_int("path set", daemon_set_rflog_path_global(path), 1);
    expect_int("start opens the file", daemon_rflog_start(err, sizeof(err)), 1);
    expect_int("active", daemon_rflog_active(), 1);

    long inode_before = file_inode(path);
    const uint8_t payload[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    /* Each line is ~110 bytes; 30 lines cross a 2000-byte cap once. */
    for (int i = 0; i < 30; i++)
        daemon_rflog_rx("433", -9000, 500, payload, sizeof(payload));

    expect_int("previous segment exists after the cap", file_size(prev) > 0, 1);
    expect_int("previous segment holds whole lines", count_lines(prev) > 0, 1);
    expect_int("live file was truncated in place, not renamed", file_inode(path), inode_before);
    expect_int("live file continues below the cap", file_size(path) < 2000, 1);
    expect_int("nothing lost: lines total 30", count_lines(prev) + count_lines(path), 30);

    /* External truncation (the controller's Clear) is tolerated: the next
     * line lands at the new end of the same inode. */
    int fd = open(path, O_WRONLY);
    expect_int("external truncate", ftruncate(fd, 0), 0);
    close(fd);
    daemon_rflog_tx("433", "ok", payload, 4);
    expect_int("one line after an external truncate", count_lines(path), 1);
    expect_int("same inode after the external truncate", file_inode(path), inode_before);

    daemon_rflog_stop();
    unlink(path);
    unlink(prev);
    rmdir(dir);
}

static void test_inactive_writer_writes_nothing(void)
{
    daemon_rflog_reset();
    const uint8_t payload[2] = {0xaa, 0xbb};
    daemon_rflog_rx("433", 0, 0, payload, 2);     /* must not crash, must not write */
    daemon_rflog_tx("433", "ok", payload, 2);
    expect_int("inactive stays inactive", daemon_rflog_active(), 0);
}

int main(void)
{
    test_switch_parse();
    test_path_must_be_absolute();
    test_on_without_path_refuses_start();
    test_off_is_a_noop();
    test_line_format();
    test_line_is_bounded();
    test_rollover_keeps_inode_and_previous_segment();
    test_inactive_writer_writes_nothing();
    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
