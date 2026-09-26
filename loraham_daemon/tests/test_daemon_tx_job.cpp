#include "../daemon_tx_job.h"

#include <stdio.h>
#include <string.h>

/* --- TX job/result prep tests ------------------------------------------ */

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

static void test_job_init(void)
{
    DaemonTxJob job;

    memset(&job, 0xff, sizeof(job));
    daemon_tx_job_init(&job, 433, 1, 42);

    expect_int("job band", job.band, 433);
    expect_int("job tx mode", job.tx_mode, 1);
    expect_int("job seq", job.seq, 42);
    expect_int("job flags default", job.flags, 0);
    expect_size("job payload len default", job.payload_len, 0);
}

static void test_payload_copy(void)
{
    DaemonTxJob job;
    const uint8_t payload[] = { 0x11, 0x22, 0x33 };

    daemon_tx_job_init(&job, 868, 0, 7);

    expect_int("payload accepted",
               daemon_tx_job_set_payload(&job, payload, sizeof(payload)), 0);
    expect_size("payload length", job.payload_len, sizeof(payload));
    expect_int("payload byte copied", job.payload[1], 0x22);
}

static void test_payload_rejects_invalid(void)
{
    DaemonTxJob job;
    uint8_t too_large[FRAMED_DATA_MAX_RF_PAYLOAD + 1];
    const uint8_t payload[] = { 0xaa };

    memset(too_large, 0, sizeof(too_large));
    daemon_tx_job_init(&job, 433, 0, 1);
    expect_int("payload seed accepted",
               daemon_tx_job_set_payload(&job, payload, sizeof(payload)), 0);
    expect_size("payload seed length", job.payload_len, sizeof(payload));

    expect_int("null payload with length rejected",
               daemon_tx_job_set_payload(&job, NULL, 1), -1);
    expect_size("null reject keeps length", job.payload_len, sizeof(payload));

    expect_int("oversize payload rejected",
               daemon_tx_job_set_payload(&job, too_large, sizeof(too_large)), -1);
    expect_size("oversize reject keeps length", job.payload_len, sizeof(payload));
}

static void test_result_init(void)
{
    DaemonTxJob job;
    DaemonTxJobResult result;

    daemon_tx_job_init(&job, 433, 1, 99);
    job.flags = FRAMED_DATA_TX_RESULT_FLAG_MANAGED;

    daemon_tx_job_result_init(&result, &job, DAEMON_TX_OUTCOME_CHANNEL_BUSY);

    expect_int("result seq", result.seq, 99);
    expect_int("result flags", result.flags, FRAMED_DATA_TX_RESULT_FLAG_MANAGED);
    expect_int("result outcome", result.outcome, DAEMON_TX_OUTCOME_CHANNEL_BUSY);
    expect_int("result tx result", result.tx_result, TX_RESULT_CAD_TIMEOUT);
    expect_int("result framed status", result.framed_status,
               FRAMED_DATA_TX_STATUS_CHANNEL_BUSY);
}

static void test_null_safe(void)
{
    DaemonTxJobResult result;

    daemon_tx_job_init(NULL, 0, 0, 0);
    expect_int("null job payload rejected",
               daemon_tx_job_set_payload(NULL, NULL, 0), -1);

    daemon_tx_job_result_init(&result, NULL, DAEMON_TX_OUTCOME_RADIO_NOT_READY);
    expect_int("null result seq", result.seq, 0);
    expect_int("null result tx result", result.tx_result,
               TX_RESULT_RADIO_NOT_READY);
    expect_int("null result framed status", result.framed_status,
               FRAMED_DATA_TX_STATUS_RADIO_NOT_READY);
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

    test_job_init();
    test_payload_copy();
    test_payload_rejects_invalid();
    test_result_init();
    test_null_safe();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
