#include "../config_stream.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

/* --- Test helpers -------------------------------------------------------- */

typedef struct {
    int count;
    char lines[4][64];
} LineRecorder;

static void record_line(const char *line, void *user)
{
    LineRecorder *rec = (LineRecorder *)user;

    assert(rec->count < 4);
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

    assert(config_stream_feed(&stream,
                              (const uint8_t *)"SET GETRSSI=",
                              strlen("SET GETRSSI="),
                              record_line,
                              &rec) == 0);
    assert(rec.count == 0);

    assert(config_stream_feed(&stream,
                              (const uint8_t *)"1\n",
                              strlen("1\n"),
                              record_line,
                              &rec) == 0);
    assert(rec.count == 1);
    assert(strcmp(rec.lines[0], "SET GETRSSI=1") == 0);
}

static void test_multiple_lines_in_one_buffer(void)
{
    ConfigStreamBuffer stream;
    LineRecorder rec = {0, {{0}}};
    const char *data = "SET GETRSSI=0\nSET GETRSSI=1\n";

    config_stream_init(&stream);

    assert(config_stream_feed(&stream,
                              (const uint8_t *)data,
                              strlen(data),
                              record_line,
                              &rec) == 0);
    assert(rec.count == 2);
    assert(strcmp(rec.lines[0], "SET GETRSSI=0") == 0);
    assert(strcmp(rec.lines[1], "SET GETRSSI=1") == 0);
}

static void test_crlf_is_accepted(void)
{
    ConfigStreamBuffer stream;
    LineRecorder rec = {0, {{0}}};
    const char *data = "SET GETRSSI=1\r\n";

    config_stream_init(&stream);

    assert(config_stream_feed(&stream,
                              (const uint8_t *)data,
                              strlen(data),
                              record_line,
                              &rec) == 0);
    assert(rec.count == 1);
    assert(strcmp(rec.lines[0], "SET GETRSSI=1") == 0);
}

static void test_flush_processes_final_unterminated_line(void)
{
    ConfigStreamBuffer stream;
    LineRecorder rec = {0, {{0}}};

    config_stream_init(&stream);

    assert(config_stream_feed(&stream,
                              (const uint8_t *)"SET POWER=10",
                              strlen("SET POWER=10"),
                              record_line,
                              &rec) == 0);
    assert(rec.count == 0);

    assert(config_stream_flush(&stream, record_line, &rec) == 0);
    assert(rec.count == 1);
    assert(strcmp(rec.lines[0], "SET POWER=10") == 0);
}

static void test_overlong_line_is_rejected(void)
{
    ConfigStreamBuffer stream;
    LineRecorder rec = {0, {{0}}};
    uint8_t data[CONFIG_STREAM_LINE_LEN];

    memset(data, 'A', sizeof(data));
    config_stream_init(&stream);

    assert(config_stream_feed(&stream,
                              data,
                              sizeof(data),
                              record_line,
                              &rec) != 0);
    assert(rec.count == 0);
}

int main(void)
{
    test_fragmented_line_is_buffered();
    test_multiple_lines_in_one_buffer();
    test_crlf_is_accepted();
    test_flush_processes_final_unterminated_line();
    test_overlong_line_is_rejected();

    return 0;
}
