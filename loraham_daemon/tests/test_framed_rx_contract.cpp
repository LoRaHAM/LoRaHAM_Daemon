#include "../framed_data.h"

#include <stdio.h>
#include <string.h>

/* --- Framed RX packet contract tests ------------------------------------
 *
 * RX_PACKET is intentionally a breaking framed-DATA interface change:
 *
 *   frame header: type + uint16 payload_len
 *   RX payload:   int16 RSSI c-dBm + int16 SNR c-dB + RF bytes
 *
 * Raw DATA clients must continue to see only the original RF bytes. This
 * test keeps the contract explicit at the frame-construction boundary.
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

static int16_t read_i16_le(const uint8_t *buf)
{
    uint16_t raw = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
    return (int16_t)raw;
}

static void test_rx_packet_contract_keeps_rf_bytes_unchanged(void)
{
    const uint8_t rf[] = { 0x3C, 0xFF, 0x01, 'D', 'J', '0', 'C', 'H', 'E' };
    uint8_t frame[FRAMED_DATA_RX_FRAME_MAX];
    uint8_t type = 0;
    uint16_t payload_len = 0;
    const uint8_t *rx_payload = frame + FRAMED_DATA_HEADER_LEN;
    const uint8_t *framed_rf = rx_payload + FRAMED_DATA_RX_META_LEN;

    expect_int("encode RX contract frame",
               framed_data_encode_rx_packet(frame,
                                            sizeof(frame),
                                            -12345,
                                            875,
                                            rf,
                                            (uint16_t)sizeof(rf)), 0);

    expect_int("decode RX contract header",
               framed_data_decode_header(frame, sizeof(frame), &type, &payload_len), 0);
    expect_int("RX contract type", type, FRAMED_DATA_TYPE_RX_PACKET);
    expect_int("RX contract payload len", payload_len,
               FRAMED_DATA_RX_META_LEN + (int)sizeof(rf));

    expect_int("RX contract RSSI", read_i16_le(rx_payload), -12345);
    expect_int("RX contract SNR", read_i16_le(rx_payload + 2), 875);

    expect_mem("framed RF bytes equal original RF bytes", framed_rf, rf, sizeof(rf));

    /*
     * Raw clients are not encoded through the framed helper. The raw contract is
     * the original RF byte span, not the metadata-prefixed RX payload.
     */
    expect_mem("raw RF bytes equal original RF bytes", rf, rf, sizeof(rf));
    expect_int("raw length excludes RX metadata", (int)sizeof(rf),
               (int)(payload_len - FRAMED_DATA_RX_META_LEN));
}

static void test_rx_packet_contract_sentinel_and_max_size(void)
{
    uint8_t rf[FRAMED_DATA_MAX_RF_PAYLOAD];
    uint8_t frame[FRAMED_DATA_RX_FRAME_MAX];
    const uint8_t *rx_payload = frame + FRAMED_DATA_HEADER_LEN;

    for (size_t i = 0; i < sizeof(rf); i++)
        rf[i] = (uint8_t)i;

    expect_int("encode RX max contract frame",
               framed_data_encode_rx_packet(frame,
                                            sizeof(frame),
                                            FRAMED_DATA_SIGNAL_UNAVAILABLE,
                                            FRAMED_DATA_SIGNAL_UNAVAILABLE,
                                            rf,
                                            (uint16_t)sizeof(rf)), 0);

    expect_size("RX max frame contract size",
                framed_data_frame_size(FRAMED_DATA_RX_PAYLOAD_MAX),
                FRAMED_DATA_RX_FRAME_MAX);

    expect_int("RX RSSI unavailable sentinel",
               read_i16_le(rx_payload),
               FRAMED_DATA_SIGNAL_UNAVAILABLE);
    expect_int("RX SNR unavailable sentinel",
               read_i16_le(rx_payload + 2),
               FRAMED_DATA_SIGNAL_UNAVAILABLE);

    expect_mem("RX max framed RF bytes intact",
               rx_payload + FRAMED_DATA_RX_META_LEN,
               rf,
               sizeof(rf));
}

static void test_tx_packet_contract_stays_metadata_free(void)
{
    const uint8_t rf[] = { 0x01, 0x02, 0x03, 0x04 };
    uint8_t frame[FRAMED_DATA_HEADER_LEN + sizeof(rf)];
    uint8_t type = 0;
    uint16_t payload_len = 0;

    expect_int("encode TX frame metadata-free",
               framed_data_encode_frame(frame,
                                        sizeof(frame),
                                        FRAMED_DATA_TYPE_TX_PACKET,
                                        rf,
                                        (uint16_t)sizeof(rf)), 0);
    expect_int("decode TX header",
               framed_data_decode_header(frame, sizeof(frame), &type, &payload_len), 0);
    expect_int("TX type", type, FRAMED_DATA_TYPE_TX_PACKET);
    expect_int("TX payload len is RF len", payload_len, (int)sizeof(rf));
    expect_mem("TX payload is RF only", frame + FRAMED_DATA_HEADER_LEN, rf, sizeof(rf));
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

    test_rx_packet_contract_keeps_rf_bytes_unchanged();
    test_rx_packet_contract_sentinel_and_max_size();
    test_tx_packet_contract_stays_metadata_free();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
