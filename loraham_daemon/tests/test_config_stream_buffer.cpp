#include "../config_stream.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* --- Test helpers -------------------------------------------------------- */

typedef struct {
    int count;
    char lines[4][64];
} LineRecorder;

static int g_ok = 0;
static int g_fail = 0;

static void expect_int(const char *name, int actual, int expected)
{
    if (actual == expected) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: expected %d, got %d\n",
               name, expected, actual);
    }
}

static void expect_str(const char *name, const char *actual, const char *expected)
{
    if (strcmp(actual, expected) == 0) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: expected %s, got %s\n",
               name, expected, actual);
    }
}

static void record_line(const char *line, void *user)
{
    LineRecorder *rec = (LineRecorder *)user;

    if (rec->count >= 4) {
        g_fail++;
        printf("[FAIL] record line capacity\n");
        return;
    }

    strncpy(rec->lines[rec->count], line, sizeof(rec->lines[rec->count]) - 1);
    rec->lines[rec->count][sizeof(rec->lines[rec->count]) - 1] = '\0';
    rec->count++;
}

/* --- Tests --------------------------------------------------------------- */

static void test_fragmented_line_is_buffered(void)
{
    ConfigStreamBuffer stream;
    LineRecorder rec = {0, {{0}}};

    config_stream_init(&stream);

    expect_int("fragment first part accepted",
               config_stream_feed(&stream,
                                  (const uint8_t *)"SET GETRSSI=",
                                  strlen("SET GETRSSI="),
                                  record_line,
                                  &rec), 0);
    expect_int("fragment first part buffered", rec.count, 0);

    expect_int("fragment second part accepted",
               config_stream_feed(&stream,
                                  (const uint8_t *)"1\n",
                                  strlen("1\n"),
                                  record_line,
                                  &rec), 0);
    expect_int("fragment emits one line", rec.count, 1);
    expect_str("fragment line content", rec.lines[0], "SET GETRSSI=1");
}

static void test_multiple_lines_in_one_buffer(void)
{
    ConfigStreamBuffer stream;
    LineRecorder rec = {0, {{0}}};
    const char *data = "SET GETRSSI=0\nSET GETRSSI=1\n";

    config_stream_init(&stream);

    expect_int("multiple lines accepted",
               config_stream_feed(&stream,
                                  (const uint8_t *)data,
                                  strlen(data),
                                  record_line,
                                  &rec), 0);
    expect_int("multiple lines count", rec.count, 2);
    expect_str("multiple line first", rec.lines[0], "SET GETRSSI=0");
    expect_str("multiple line second", rec.lines[1], "SET GETRSSI=1");
}

static void test_crlf_is_accepted(void)
{
    ConfigStreamBuffer stream;
    LineRecorder rec = {0, {{0}}};
    const char *data = "SET GETRSSI=1\r\n";

    config_stream_init(&stream);

    expect_int("crlf accepted",
               config_stream_feed(&stream,
                                  (const uint8_t *)data,
                                  strlen(data),
                                  record_line,
                                  &rec), 0);
    expect_int("crlf count", rec.count, 1);
    expect_str("crlf line content", rec.lines[0], "SET GETRSSI=1");
}

static void test_flush_processes_final_unterminated_line(void)
{
    ConfigStreamBuffer stream;
    LineRecorder rec = {0, {{0}}};

    config_stream_init(&stream);

    expect_int("unterminated accepted",
               config_stream_feed(&stream,
                                  (const uint8_t *)"SET POWER=10",
                                  strlen("SET POWER=10"),
                                  record_line,
                                  &rec), 0);
    expect_int("unterminated initially buffered", rec.count, 0);

    expect_int("flush accepted",
               config_stream_flush(&stream, record_line, &rec), 0);
    expect_int("flush emits one line", rec.count, 1);
    expect_str("flush line content", rec.lines[0], "SET POWER=10");
}

static void test_overlong_line_is_rejected(void)
{
    ConfigStreamBuffer stream;
    LineRecorder rec = {0, {{0}}};
    uint8_t data[CONFIG_STREAM_LINE_LEN];

    memset(data, 'A', sizeof(data));
    config_stream_init(&stream);

    expect_int("overlong rejected",
               config_stream_feed(&stream,
                                  data,
                                  sizeof(data),
                                  record_line,
                                  &rec) != 0, 1);
    expect_int("overlong emits no line", rec.count, 0);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--bin") == 0) {
            if (i + 1 >= argc) {
                printf("Usage: %s [--bin ignored]\n", argv[0]);
                return 2;
            }
            i++;
        } else if (strcmp(argv[i], "--help") == 0 ||
                   strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [--bin ignored]\n", argv[0]);
            return 0;
        } else {
            printf("Usage: %s [--bin ignored]\n", argv[0]);
            return 2;
        }
    }

    test_fragmented_line_is_buffered();
    test_multiple_lines_in_one_buffer();
    test_crlf_is_accepted();
    test_flush_processes_final_unterminated_line();
    test_overlong_line_is_rejected();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
