/*
 * How many payload bytes one frame may carry (hardware audit HW-5).
 *
 * The SX127x FSK FIFO is 64 bytes, and variable-length packet mode puts the
 * length byte INTO that FIFO, so the largest payload that fits is 63. The
 * daemon accepted 255 in every mode, and an oversized FSK frame reached the
 * radio to fail there.
 *
 * The rule is deliberately NOT a narrowing of the three 255 constants --
 * RF_PACKET_MAX_PAYLOAD_LEN is an absolute buffer maximum,
 * FRAMED_DATA_MAX_RF_PAYLOAD is the framing layer's storage ceiling, and
 * DATA_TX_MAX_CHUNK_SIZE is the chunker's. Narrowing any of them to 63 would
 * cripple LoRa and turn a radio constraint into a wire-protocol constraint.
 */

#include "../radio_tx_limit.h"
#include "../data_tx.h"
#include "../framed_data.h"
#include "../rf_packet.h"

#include <stdio.h>

static int g_ok = 0;
static int g_fail = 0;

static void expect_size(const char *name, size_t got, size_t want)
{
    if (got == want) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: got %zu, want %zu\n", name, got, want);
    }
}

/*
 * The pure rule. It narrows exactly one combination and nothing else: a rule
 * that also clipped LoRa, or SX1262's larger FSK FIFO, would be a regression
 * dressed as a fix.
 */
static void test_the_rule_narrows_only_sx127x_fsk(void)
{
    expect_size("SX127x + FSK is the FIFO minus the length byte",
                radio_tx_payload_limit(DAEMON_CHIP_FAMILY_SX127X,
                                       RADIO_MODE_FSK), 63);
    expect_size("SX127x + LoRa keeps the full payload",
                radio_tx_payload_limit(DAEMON_CHIP_FAMILY_SX127X,
                                       RADIO_MODE_LORA),
                RF_PACKET_MAX_PAYLOAD_LEN);
    expect_size("SX1262 + FSK is not an SX127x FIFO",
                radio_tx_payload_limit(DAEMON_CHIP_FAMILY_SX1262,
                                       RADIO_MODE_FSK),
                RF_PACKET_MAX_PAYLOAD_LEN);
    expect_size("SX1262 + LoRa keeps the full payload",
                radio_tx_payload_limit(DAEMON_CHIP_FAMILY_SX1262,
                                       RADIO_MODE_LORA),
                RF_PACKET_MAX_PAYLOAD_LEN);
}

/*
 * The live wrapper narrows; it never invents a limit where there is no radio
 * to ask. A caller with no controller must not silently start chunking at 63.
 */
static void test_the_live_wrapper_needs_a_radio(void)
{
    expect_size("no controller -> the absolute maximum",
                radio_tx_payload_limit((const RadioController *)NULL),
                RF_PACKET_MAX_PAYLOAD_LEN);
}

/*
 * The storage constants stay where they are. This is an assertion about
 * intent: if a later change narrows one of them to 63, LoRa loses 192 bytes
 * of payload and the framing layer's RX ceiling moves with it.
 */
static void test_the_storage_ceilings_are_untouched(void)
{
    expect_size("the buffer maximum is still 255", RF_PACKET_MAX_PAYLOAD_LEN, 255);
    expect_size("the framing ceiling is still 255", FRAMED_DATA_MAX_RF_PAYLOAD, 255);
    expect_size("the chunker ceiling is still 255", DATA_TX_MAX_CHUNK_SIZE, 255);
}

int main(void)
{
    test_the_rule_narrows_only_sx127x_fsk();
    test_the_live_wrapper_needs_a_radio();
    test_the_storage_ceilings_are_untouched();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
