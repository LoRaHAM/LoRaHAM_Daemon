#include "../daemon_tx_outcome.h"

#include <stdio.h>
#include <string.h>

/* --- TX outcome mapping tests ------------------------------------------ */

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

static void test_outcome_flags(void)
{
    expect_int("outcome ok is ok",
               daemon_tx_outcome_is_ok(DAEMON_TX_OUTCOME_OK), 1);
    expect_int("outcome busy is not ok",
               daemon_tx_outcome_is_ok(DAEMON_TX_OUTCOME_BUSY), 0);
    expect_int("outcome failure",
               daemon_tx_outcome_is_failure(DAEMON_TX_OUTCOME_RADIO_ERROR), 1);
}

static void test_tx_result_to_outcome(void)
{
    expect_int("tx ok outcome",
               daemon_tx_outcome_from_tx_result(TX_RESULT_OK),
               DAEMON_TX_OUTCOME_OK);
    expect_int("tx busy outcome",
               daemon_tx_outcome_from_tx_result(TX_RESULT_BUSY),
               DAEMON_TX_OUTCOME_BUSY);
    expect_int("cad timeout outcome",
               daemon_tx_outcome_from_tx_result(TX_RESULT_CAD_TIMEOUT),
               DAEMON_TX_OUTCOME_CHANNEL_BUSY);
    expect_int("not ready outcome",
               daemon_tx_outcome_from_tx_result(TX_RESULT_RADIO_NOT_READY),
               DAEMON_TX_OUTCOME_RADIO_NOT_READY);
    expect_int("invalid packet outcome",
               daemon_tx_outcome_from_tx_result(TX_RESULT_INVALID_PACKET),
               DAEMON_TX_OUTCOME_INVALID_PACKET);
    expect_int("invalid band outcome",
               daemon_tx_outcome_from_tx_result(TX_RESULT_INVALID_BAND),
               DAEMON_TX_OUTCOME_INVALID_BAND);
    expect_int("radio error outcome",
               daemon_tx_outcome_from_tx_result(TX_RESULT_RADIO_ERROR),
               DAEMON_TX_OUTCOME_RADIO_ERROR);
}

static void test_outcome_to_framed_status(void)
{
    expect_int("framed ok",
               daemon_tx_outcome_to_framed_status(DAEMON_TX_OUTCOME_OK),
               FRAMED_DATA_TX_STATUS_OK);
    expect_int("framed busy",
               daemon_tx_outcome_to_framed_status(DAEMON_TX_OUTCOME_BUSY),
               FRAMED_DATA_TX_STATUS_BUSY);
    expect_int("framed channel busy",
               daemon_tx_outcome_to_framed_status(DAEMON_TX_OUTCOME_CHANNEL_BUSY),
               FRAMED_DATA_TX_STATUS_CHANNEL_BUSY);
    expect_int("framed radio not ready",
               daemon_tx_outcome_to_framed_status(DAEMON_TX_OUTCOME_RADIO_NOT_READY),
               FRAMED_DATA_TX_STATUS_RADIO_NOT_READY);
    expect_int("framed invalid packet",
               daemon_tx_outcome_to_framed_status(DAEMON_TX_OUTCOME_INVALID_PACKET),
               FRAMED_DATA_TX_STATUS_INVALID_PACKET);
    expect_int("framed invalid band",
               daemon_tx_outcome_to_framed_status(DAEMON_TX_OUTCOME_INVALID_BAND),
               FRAMED_DATA_TX_STATUS_INVALID_BAND);
    expect_int("framed radio error",
               daemon_tx_outcome_to_framed_status(DAEMON_TX_OUTCOME_RADIO_ERROR),
               FRAMED_DATA_TX_STATUS_RADIO_ERROR);
    expect_int("framed unknown fallback",
               daemon_tx_outcome_to_framed_status(999),
               FRAMED_DATA_TX_STATUS_RADIO_ERROR);
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

    test_outcome_flags();
    test_tx_result_to_outcome();
    test_outcome_to_framed_status();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
