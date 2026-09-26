#include "../framed_data_tx.h"

#include <stdio.h>
#include <string.h>

/* --- Framed DATA TX state tests ----------------------------------------- */

static int g_ok = 0;
static int g_fail = 0;

typedef struct {
    int tx_count;
    int error_count;
    size_t last_len;
    uint8_t last_payload[FRAMED_DATA_MAX_RF_PAYLOAD];
} TestCtx;

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

static void expect_size(const char *name, size_t actual, size_t expected)
{
    if (actual == expected) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: expected %zu, got %zu\n", name, expected, actual);
    }
}

static int on_tx(uint8_t *payload, size_t len, void *ctx)
{
    TestCtx *test = (TestCtx *)ctx;

    test->tx_count++;
    test->last_len = len;
    if (len > 0)
        memcpy(test->last_payload, payload, len);

    return 0;
}

static int on_tx_fail(uint8_t *payload, size_t len, void *ctx)
{
    (void)payload;
    (void)len;
    (void)ctx;

    return -1;
}

static void on_error(const char *msg, void *ctx)
{
    TestCtx *test = (TestCtx *)ctx;

    (void)msg;
    test->error_count++;
}

static void encode_tx(uint8_t *frame,
                      const uint8_t *payload,
                      uint16_t len)
{
    framed_data_encode_header(frame, FRAMED_DATA_HEADER_LEN,
                              FRAMED_DATA_TYPE_TX_PACKET, len);
    if (len > 0)
        memcpy(frame + FRAMED_DATA_HEADER_LEN, payload, len);
}

static void test_complete_tx_frame(void)
{
    FramedDataTxState state;
    TestCtx ctx = {};
    const uint8_t payload[] = { 0x11, 0x22, 0x33 };
    uint8_t frame[FRAMED_DATA_HEADER_LEN + sizeof(payload)];

    framed_data_tx_state_init(&state);
    encode_tx(frame, payload, sizeof(payload));

    expect_int("complete TX feed ok",
               framed_data_tx_feed_state(&state, frame, sizeof(frame),
                                         on_tx, &ctx, on_error, &ctx), 0);
    expect_int("complete TX count", ctx.tx_count, 1);
    expect_int("complete TX errors", ctx.error_count, 0);
    expect_size("complete TX len", ctx.last_len, sizeof(payload));
    expect_int("complete TX payload[1]", ctx.last_payload[1], 0x22);
}

static void test_split_tx_frame(void)
{
    FramedDataTxState state;
    TestCtx ctx = {};
    const uint8_t payload[] = { 0x44, 0x55 };
    uint8_t frame[FRAMED_DATA_HEADER_LEN + sizeof(payload)];

    framed_data_tx_state_init(&state);
    encode_tx(frame, payload, sizeof(payload));

    expect_int("split header feed ok",
               framed_data_tx_feed_state(&state, frame, 2,
                                         on_tx, &ctx, on_error, &ctx), 0);
    expect_int("split no early TX", ctx.tx_count, 0);

    expect_int("split tail feed ok",
               framed_data_tx_feed_state(&state, frame + 2, sizeof(frame) - 2,
                                         on_tx, &ctx, on_error, &ctx), 0);
    expect_int("split TX count", ctx.tx_count, 1);
    expect_size("split TX len", ctx.last_len, sizeof(payload));
    expect_int("split TX payload[0]", ctx.last_payload[0], 0x44);
}

static void test_unsupported_client_frame_reports_error(void)
{
    FramedDataTxState state;
    TestCtx ctx = {};
    uint8_t frame[FRAMED_DATA_HEADER_LEN + 1];

    framed_data_tx_state_init(&state);
    framed_data_encode_header(frame, FRAMED_DATA_HEADER_LEN,
                              FRAMED_DATA_TYPE_ERROR, 1);
    frame[FRAMED_DATA_HEADER_LEN] = 0x99;

    expect_int("unsupported frame feed ok",
               framed_data_tx_feed_state(&state, frame, sizeof(frame),
                                         on_tx, &ctx, on_error, &ctx), 0);
    expect_int("unsupported no TX", ctx.tx_count, 0);
    expect_int("unsupported error", ctx.error_count, 1);
}

static void test_oversized_tx_reports_error(void)
{
    FramedDataTxState state;
    TestCtx ctx = {};
    uint8_t header[FRAMED_DATA_HEADER_LEN] = {
        FRAMED_DATA_TYPE_TX_PACKET,
        0x00,
        0x01
    };

    framed_data_tx_state_init(&state);

    expect_int("oversized TX header feed ok",
               framed_data_tx_feed_state(&state, header, sizeof(header),
                                         on_tx, &ctx, on_error, &ctx), 0);
    expect_int("oversized no TX", ctx.tx_count, 0);
    expect_int("oversized error", ctx.error_count, 1);
}

static void test_handler_failure_is_reported(void)
{
    FramedDataTxState state;
    TestCtx ctx = {};
    const uint8_t payload[] = { 0x66 };
    uint8_t frame[FRAMED_DATA_HEADER_LEN + sizeof(payload)];

    framed_data_tx_state_init(&state);
    encode_tx(frame, payload, sizeof(payload));

    expect_int("handler failure feed ok",
               framed_data_tx_feed_state(&state, frame, sizeof(frame),
                                         on_tx_fail, &ctx, on_error, &ctx), 0);
    expect_int("handler failure error reported", ctx.error_count, 1);
}

// A rejected frame must produce exactly ONE error and the parser must
// re-sync after the declared payload length: a valid frame following the
// junk in the same feed is parsed normally (no per-byte error flood).
static void test_invalid_frame_skips_declared_payload(void)
{
    FramedDataTxState state;
    TestCtx ctx = {};
    const uint8_t good_payload[] = { 0x42, 0x43 };
    uint8_t buf[3 + 300 + FRAMED_DATA_HEADER_LEN + sizeof(good_payload)];

    framed_data_tx_state_init(&state);

    /* Oversized TX_PACKET: declared 300 bytes of junk payload... */
    buf[0] = FRAMED_DATA_TYPE_TX_PACKET;
    buf[1] = 0x2C;                      /* 300 little-endian */
    buf[2] = 0x01;
    memset(buf + 3, 'X', 300);
    /* ...directly followed by a valid 2-byte TX frame. */
    encode_tx(buf + 3 + 300, good_payload, sizeof(good_payload));

    expect_int("skip feed ok",
               framed_data_tx_feed_state(&state, buf, sizeof(buf),
                                         on_tx, &ctx, on_error, &ctx), 0);
    expect_int("skip exactly one error", ctx.error_count, 1);
    expect_int("skip following frame delivered", ctx.tx_count, 1);
    expect_int("skip following frame length", (int)ctx.last_len,
               (int)sizeof(good_payload));

    /* Unknown frame type with declared payload: same contract. */
    TestCtx ctx2 = {};
    framed_data_tx_state_init(&state);
    buf[0] = 0x7F;
    buf[1] = 0x04;
    buf[2] = 0x00;
    memset(buf + 3, 'A', 4);
    encode_tx(buf + 3 + 4, good_payload, sizeof(good_payload));

    expect_int("unknown-skip feed ok",
               framed_data_tx_feed_state(&state, buf,
                                         3 + 4 + FRAMED_DATA_HEADER_LEN +
                                             sizeof(good_payload),
                                         on_tx, &ctx2, on_error, &ctx2), 0);
    expect_int("unknown-skip one error", ctx2.error_count, 1);
    expect_int("unknown-skip following frame delivered", ctx2.tx_count, 1);
}

static void test_null_arguments(void)
{
    FramedDataTxState state;
    uint8_t byte = 0;

    framed_data_tx_state_init(&state);

    expect_int("null state rejected",
               framed_data_tx_feed_state(NULL, &byte, 1,
                                         on_tx, NULL, on_error, NULL), -1);
    expect_int("null buffer with data rejected",
               framed_data_tx_feed_state(&state, NULL, 1,
                                         on_tx, NULL, on_error, NULL), -1);
    expect_int("null buffer with zero ok",
               framed_data_tx_feed_state(&state, NULL, 0,
                                         on_tx, NULL, on_error, NULL), 0);
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

    test_complete_tx_frame();
    test_split_tx_frame();
    test_unsupported_client_frame_reports_error();
    test_oversized_tx_reports_error();
    test_handler_failure_is_reported();
    test_invalid_frame_skips_declared_payload();
    test_null_arguments();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
