#include "../framed_data_tx.h"

#include <stdio.h>
#include <string.h>

/*
 * M1 regression guard for the CAD/TX signaling rework.
 *
 * A recoverable RF/TX execution failure must be reported to the framed client
 * and must not make the framed TX parser return a fatal error to the socket
 * runtime. Until TX_RESULT exists, the report is the existing ERROR path.
 */

static int g_ok = 0;
static int g_fail = 0;

static void expect_int(const char *name, int actual, int expected)
{
    if (actual == expected) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: expected %d, got %d\n", name, expected, actual);
    }
}

static int failing_rf_tx(uint8_t *payload, size_t len, void *ctx)
{
    (void)payload;
    (void)len;
    (void)ctx;
    return -1;
}

static void record_error(const char *msg, void *ctx)
{
    int *count = (int *)ctx;
    (void)msg;
    (*count)++;
}

static void make_tx_frame(uint8_t *frame, const uint8_t *payload, uint16_t len)
{
    framed_data_encode_header(frame,
                              FRAMED_DATA_HEADER_LEN,
                              FRAMED_DATA_TYPE_TX_PACKET,
                              len);
    if (len > 0)
        memcpy(frame + FRAMED_DATA_HEADER_LEN, payload, len);
}

static void test_tx_failure_keeps_parser_alive(void)
{
    FramedDataTxState state;
    const uint8_t payload[] = { 0x42 };
    uint8_t frame[FRAMED_DATA_HEADER_LEN + sizeof(payload)];
    int error_count = 0;
    int rc;

    framed_data_tx_state_init(&state);
    make_tx_frame(frame, payload, (uint16_t)sizeof(payload));

    rc = framed_data_tx_feed_state(&state,
                                   frame,
                                   sizeof(frame),
                                   failing_rf_tx,
                                   NULL,
                                   record_error,
                                   &error_count);

    expect_int("RF TX failure keeps parser alive", rc, 0);
    expect_int("RF TX failure reports ERROR", error_count, 1);
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

    test_tx_failure_keeps_parser_alive();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
