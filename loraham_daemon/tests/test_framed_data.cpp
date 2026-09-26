#include "../framed_data.h"

#include <stdio.h>
#include <string.h>

/* --- Framed DATA protocol helper tests ---------------------------------- */

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

static void expect_mem(const char *name,
                       const uint8_t *actual,
                       const uint8_t *expected,
                       size_t len)
{
    if (memcmp(actual, expected, len) == 0) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: memory mismatch\n", name);
    }
}

static void expect_i16_le(const char *name,
                          const uint8_t *buf,
                          size_t off,
                          int16_t expected)
{
    uint16_t raw = (uint16_t)expected;
    char lo_name[128];
    char hi_name[128];

    snprintf(lo_name, sizeof(lo_name), "%s low", name);
    snprintf(hi_name, sizeof(hi_name), "%s high", name);

    expect_int(lo_name, buf[off], (int)(raw & 0xFF));
    expect_int(hi_name, buf[off + 1], (int)((raw >> 8) & 0xFF));
}

static void test_frame_type_contract(void)
{
    expect_int("RX type known", framed_data_type_known(FRAMED_DATA_TYPE_RX_PACKET), 1);
    expect_int("TX type known", framed_data_type_known(FRAMED_DATA_TYPE_TX_PACKET), 1);
    expect_int("ERROR type known", framed_data_type_known(FRAMED_DATA_TYPE_ERROR), 1);
    expect_int("TX_RESULT type known", framed_data_type_known(FRAMED_DATA_TYPE_TX_RESULT), 1);
    expect_int("unknown type rejected", framed_data_type_known(0x7F), 0);
}

static void test_rf_payload_limit(void)
{
    expect_int("zero RF payload valid", framed_data_rf_payload_len_valid(0), 1);
    expect_int("max RF payload valid", framed_data_rf_payload_len_valid(255), 1);
    expect_int("oversized RF payload invalid", framed_data_rf_payload_len_valid(256), 0);
}

static void test_header_encode_decode_little_endian(void)
{
    uint8_t header[FRAMED_DATA_HEADER_LEN];
    uint8_t type = 0;
    uint16_t len = 0;

    expect_int("encode RX header", framed_data_encode_header(header, sizeof(header),
                                                             FRAMED_DATA_TYPE_RX_PACKET,
                                                             FRAMED_DATA_RX_META_LEN + 5), 0);
    expect_int("encoded type", header[0], FRAMED_DATA_TYPE_RX_PACKET);
    expect_int("encoded len low", header[1], FRAMED_DATA_RX_META_LEN + 5);
    expect_int("encoded len high", header[2], 0);

    expect_int("decode RX header", framed_data_decode_header(header, sizeof(header),
                                                             &type, &len), 0);
    expect_int("decoded type", type, FRAMED_DATA_TYPE_RX_PACKET);
    expect_int("decoded len", len, FRAMED_DATA_RX_META_LEN + 5);
}

static void test_frame_size(void)
{
    expect_size("empty frame size", framed_data_frame_size(0), FRAMED_DATA_HEADER_LEN);
    expect_size("max TX frame size", framed_data_frame_size(255),
                FRAMED_DATA_HEADER_LEN + 255);
    expect_size("RX meta length", FRAMED_DATA_RX_META_LEN, 4);
    expect_size("TX_RESULT payload length", FRAMED_DATA_TX_RESULT_PAYLOAD_LEN, 4);
    expect_size("TX_RESULT frame size",
                framed_data_frame_size(FRAMED_DATA_TX_RESULT_PAYLOAD_LEN),
                FRAMED_DATA_HEADER_LEN + FRAMED_DATA_TX_RESULT_PAYLOAD_LEN);
    expect_size("max RX payload size", FRAMED_DATA_RX_PAYLOAD_MAX,
                FRAMED_DATA_RX_META_LEN + 255);
    expect_size("max RX frame size", FRAMED_DATA_RX_FRAME_MAX,
                FRAMED_DATA_HEADER_LEN + FRAMED_DATA_RX_META_LEN + 255);
}

static void test_reject_invalid_headers(void)
{
    uint8_t header[FRAMED_DATA_HEADER_LEN];
    uint8_t type = 0;
    uint16_t len = 0;

    expect_int("encode unknown type fails",
               framed_data_encode_header(header, sizeof(header), 0x7F, 1), -1);
    expect_int("encode TX oversized fails",
               framed_data_encode_header(header, sizeof(header),
                                         FRAMED_DATA_TYPE_TX_PACKET, 256), -1);
    expect_int("encode RX missing metadata fails",
               framed_data_encode_header(header, sizeof(header),
                                         FRAMED_DATA_TYPE_RX_PACKET,
                                         FRAMED_DATA_RX_META_LEN - 1), -1);
    expect_int("encode RX max payload ok",
               framed_data_encode_header(header, sizeof(header),
                                         FRAMED_DATA_TYPE_RX_PACKET,
                                         FRAMED_DATA_RX_PAYLOAD_MAX), 0);
    expect_int("encode RX oversized fails",
               framed_data_encode_header(header, sizeof(header),
                                         FRAMED_DATA_TYPE_RX_PACKET,
                                         FRAMED_DATA_RX_PAYLOAD_MAX + 1), -1);
    expect_int("encode ERROR larger text ok",
               framed_data_encode_header(header, sizeof(header),
                                         FRAMED_DATA_TYPE_ERROR, 512), 0);
    expect_int("encode TX_RESULT len ok",
               framed_data_encode_header(header, sizeof(header),
                                         FRAMED_DATA_TYPE_TX_RESULT,
                                         FRAMED_DATA_TX_RESULT_PAYLOAD_LEN), 0);
    expect_int("encode TX_RESULT short fails",
               framed_data_encode_header(header, sizeof(header),
                                         FRAMED_DATA_TYPE_TX_RESULT,
                                         FRAMED_DATA_TX_RESULT_PAYLOAD_LEN - 1), -1);
    expect_int("decode short header fails",
               framed_data_decode_header(header, FRAMED_DATA_HEADER_LEN - 1,
                                         &type, &len), -1);

    header[0] = FRAMED_DATA_TYPE_TX_PACKET;
    header[1] = 0x00;
    header[2] = 0x01;
    expect_int("decode TX oversized fails",
               framed_data_decode_header(header, sizeof(header), &type, &len), -1);

    header[0] = FRAMED_DATA_TYPE_RX_PACKET;
    header[1] = (uint8_t)((FRAMED_DATA_RX_META_LEN - 1) & 0xFF);
    header[2] = 0;
    expect_int("decode RX missing metadata fails",
               framed_data_decode_header(header, sizeof(header), &type, &len), -1);

    header[0] = FRAMED_DATA_TYPE_TX_RESULT;
    header[1] = 0;
    header[2] = 0;
    expect_int("decode TX_RESULT wrong len fails",
               framed_data_decode_header(header, sizeof(header), &type, &len), -1);
}

static void test_frame_encode_builder(void)
{
    const uint8_t payload[] = { 0xAA, 0xBB, 0xCC };
    uint8_t frame[FRAMED_DATA_HEADER_LEN + sizeof(payload)];
    uint8_t small[FRAMED_DATA_HEADER_LEN + sizeof(payload) - 1];

    expect_int("encode TX frame ok",
               framed_data_encode_frame(frame, sizeof(frame),
                                        FRAMED_DATA_TYPE_TX_PACKET,
                                        payload,
                                        (uint16_t)sizeof(payload)), 0);
    expect_int("frame type", frame[0], FRAMED_DATA_TYPE_TX_PACKET);
    expect_int("frame len low", frame[1], (int)sizeof(payload));
    expect_int("frame len high", frame[2], 0);
    expect_int("frame payload[0]", frame[3], 0xAA);
    expect_int("frame payload[2]", frame[5], 0xCC);

    expect_int("encode frame small buffer fails",
               framed_data_encode_frame(small, sizeof(small),
                                        FRAMED_DATA_TYPE_TX_PACKET,
                                        payload,
                                        (uint16_t)sizeof(payload)), -1);
    expect_int("encode frame null payload fails",
               framed_data_encode_frame(frame, sizeof(frame),
                                        FRAMED_DATA_TYPE_TX_PACKET,
                                        NULL,
                                        (uint16_t)sizeof(payload)), -1);
}

static void test_tx_result_encode(void)
{
    uint8_t frame[FRAMED_DATA_HEADER_LEN + FRAMED_DATA_TX_RESULT_PAYLOAD_LEN];
    uint8_t small[sizeof(frame) - 1];
    uint8_t type = 0;
    uint16_t len = 0;

    expect_int("TX_RESULT OK status valid",
               framed_data_tx_result_status_valid(FRAMED_DATA_TX_STATUS_OK), 1);
    expect_int("TX_RESULT invalid band status valid",
               framed_data_tx_result_status_valid(FRAMED_DATA_TX_STATUS_INVALID_BAND), 1);
    expect_int("TX_RESULT status 7 invalid",
               framed_data_tx_result_status_valid(7), 0);

    expect_int("encode TX_RESULT ok",
               framed_data_encode_tx_result(frame,
                                            sizeof(frame),
                                            FRAMED_DATA_TX_STATUS_CHANNEL_BUSY,
                                            FRAMED_DATA_TX_RESULT_FLAG_MANAGED |
                                            FRAMED_DATA_TX_RESULT_FLAG_DEFERRED,
                                            0x1234), 0);
    expect_int("TX_RESULT frame type", frame[0], FRAMED_DATA_TYPE_TX_RESULT);
    expect_int("TX_RESULT len low", frame[1], FRAMED_DATA_TX_RESULT_PAYLOAD_LEN);
    expect_int("TX_RESULT len high", frame[2], 0);
    expect_int("TX_RESULT status", frame[3], FRAMED_DATA_TX_STATUS_CHANNEL_BUSY);
    expect_int("TX_RESULT flags", frame[4],
               FRAMED_DATA_TX_RESULT_FLAG_MANAGED |
               FRAMED_DATA_TX_RESULT_FLAG_DEFERRED);
    expect_int("TX_RESULT seq low", frame[5], 0x34);
    expect_int("TX_RESULT seq high", frame[6], 0x12);

    expect_int("decode TX_RESULT header",
               framed_data_decode_header(frame, sizeof(frame), &type, &len), 0);
    expect_int("decoded TX_RESULT type", type, FRAMED_DATA_TYPE_TX_RESULT);
    expect_int("decoded TX_RESULT len", len, FRAMED_DATA_TX_RESULT_PAYLOAD_LEN);


    uint8_t cad_frame[FRAMED_DATA_HEADER_LEN + FRAMED_DATA_TX_RESULT_PAYLOAD_LEN];

    expect_int("encode TX_RESULT cad timeout flag ok",
               framed_data_encode_tx_result(cad_frame,
                                            sizeof(cad_frame),
                                            FRAMED_DATA_TX_STATUS_OK,
                                            FRAMED_DATA_TX_RESULT_FLAG_MANAGED |
                                            FRAMED_DATA_TX_RESULT_FLAG_CAD_TIMEOUT,
                                            2), 0);
    expect_int("TX_RESULT cad timeout flag",
               cad_frame[4] & FRAMED_DATA_TX_RESULT_FLAG_CAD_TIMEOUT,
               FRAMED_DATA_TX_RESULT_FLAG_CAD_TIMEOUT);

    expect_int("encode TX_RESULT small buffer fails",
               framed_data_encode_tx_result(small,
                                            sizeof(small),
                                            FRAMED_DATA_TX_STATUS_OK,
                                            0,
                                            1), -1);
    expect_int("encode TX_RESULT invalid status fails",
               framed_data_encode_tx_result(frame,
                                            sizeof(frame),
                                            7,
                                            0,
                                            1), -1);
    expect_int("encode TX_RESULT null frame fails",
               framed_data_encode_tx_result(NULL,
                                            sizeof(frame),
                                            FRAMED_DATA_TX_STATUS_OK,
                                            0,
                                            1), -1);
}

static void test_rx_packet_encode_metadata_and_payload(void)
{
    const uint8_t payload[] = { 0xAA, 0xBB, 0xCC };
    uint8_t frame[FRAMED_DATA_RX_FRAME_MAX];
    uint8_t type = 0;
    uint16_t len = 0;

    expect_int("encode RX packet with metadata ok",
               framed_data_encode_rx_packet(frame,
                                            sizeof(frame),
                                            -1234,
                                            567,
                                            payload,
                                            (uint16_t)sizeof(payload)), 0);

    expect_int("RX frame type", frame[0], FRAMED_DATA_TYPE_RX_PACKET);
    expect_int("RX frame len low", frame[1], FRAMED_DATA_RX_META_LEN + (int)sizeof(payload));
    expect_int("RX frame len high", frame[2], 0);
    expect_i16_le("RX RSSI little endian", frame, FRAMED_DATA_HEADER_LEN, -1234);
    expect_i16_le("RX SNR little endian", frame, FRAMED_DATA_HEADER_LEN + 2, 567);
    expect_mem("RX RF bytes intact",
               frame + FRAMED_DATA_HEADER_LEN + FRAMED_DATA_RX_META_LEN,
               payload,
               sizeof(payload));

    expect_int("decode encoded RX header",
               framed_data_decode_header(frame, sizeof(frame), &type, &len), 0);
    expect_int("decoded RX type", type, FRAMED_DATA_TYPE_RX_PACKET);
    expect_int("decoded RX payload len", len,
               FRAMED_DATA_RX_META_LEN + (int)sizeof(payload));
}

static void test_rx_packet_boundaries(void)
{
    uint8_t payload[FRAMED_DATA_MAX_RF_PAYLOAD + 1];
    uint8_t frame[FRAMED_DATA_RX_FRAME_MAX];
    uint8_t small[FRAMED_DATA_RX_FRAME_MAX - 1];

    for (size_t i = 0; i < sizeof(payload); i++)
        payload[i] = (uint8_t)i;

    expect_int("encode RX zero RF payload ok",
               framed_data_encode_rx_packet(frame,
                                            sizeof(frame),
                                            32767,
                                            FRAMED_DATA_SIGNAL_UNAVAILABLE,
                                            NULL,
                                            0), 0);
    expect_int("zero RF RX payload len low", frame[1], FRAMED_DATA_RX_META_LEN);
    expect_int("zero RF RX payload len high", frame[2], 0);
    expect_i16_le("RX max positive RSSI", frame, FRAMED_DATA_HEADER_LEN, 32767);
    expect_i16_le("RX unavailable SNR", frame, FRAMED_DATA_HEADER_LEN + 2,
                  FRAMED_DATA_SIGNAL_UNAVAILABLE);

    expect_int("encode RX max RF payload ok",
               framed_data_encode_rx_packet(frame,
                                            sizeof(frame),
                                            FRAMED_DATA_SIGNAL_UNAVAILABLE,
                                            -250,
                                            payload,
                                            FRAMED_DATA_MAX_RF_PAYLOAD), 0);
    expect_int("max RX payload len low", frame[1], FRAMED_DATA_RX_PAYLOAD_MAX & 0xFF);
    expect_int("max RX payload len high", frame[2], (FRAMED_DATA_RX_PAYLOAD_MAX >> 8) & 0xFF);
    expect_int("max RX last RF byte",
               frame[FRAMED_DATA_RX_FRAME_MAX - 1],
               payload[FRAMED_DATA_MAX_RF_PAYLOAD - 1]);
    expect_size("max RX encoded frame size",
                framed_data_frame_size(FRAMED_DATA_RX_PAYLOAD_MAX),
                FRAMED_DATA_RX_FRAME_MAX);

    expect_int("encode RX max into small buffer fails",
               framed_data_encode_rx_packet(small,
                                            sizeof(small),
                                            0,
                                            0,
                                            payload,
                                            FRAMED_DATA_MAX_RF_PAYLOAD), -1);
    expect_int("encode RX oversized RF payload fails",
               framed_data_encode_rx_packet(frame,
                                            sizeof(frame),
                                            0,
                                            0,
                                            payload,
                                            FRAMED_DATA_MAX_RF_PAYLOAD + 1), -1);
}

static void test_null_arguments(void)
{
    uint8_t header[FRAMED_DATA_HEADER_LEN];
    uint8_t frame[FRAMED_DATA_RX_FRAME_MAX];
    uint8_t type = 0;
    uint16_t len = 0;

    expect_int("encode null header fails",
               framed_data_encode_header(NULL, sizeof(header),
                                         FRAMED_DATA_TYPE_TX_PACKET, 1), -1);
    expect_int("encode short buffer fails",
               framed_data_encode_header(header, FRAMED_DATA_HEADER_LEN - 1,
                                         FRAMED_DATA_TYPE_TX_PACKET, 1), -1);
    expect_int("decode null header fails",
               framed_data_decode_header(NULL, sizeof(header), &type, &len), -1);
    expect_int("decode null type fails",
               framed_data_decode_header(header, sizeof(header), NULL, &len), -1);
    expect_int("decode null len fails",
               framed_data_decode_header(header, sizeof(header), &type, NULL), -1);
    expect_int("encode null RX frame fails",
               framed_data_encode_rx_packet(NULL, sizeof(frame), 0, 0, NULL, 0), -1);
    expect_int("encode RX null RF payload fails",
               framed_data_encode_rx_packet(frame, sizeof(frame), 0, 0, NULL, 1), -1);
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

    test_frame_type_contract();
    test_rf_payload_limit();
    test_header_encode_decode_little_endian();
    test_frame_size();
    test_reject_invalid_headers();
    test_frame_encode_builder();
    test_tx_result_encode();
    test_rx_packet_encode_metadata_and_payload();
    test_rx_packet_boundaries();
    test_null_arguments();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
