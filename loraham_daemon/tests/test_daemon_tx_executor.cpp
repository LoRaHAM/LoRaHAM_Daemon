#include "../daemon_tx_executor.h"

#include <stdio.h>
#include <string.h>

/* --- TX executor seam tests -------------------------------------------- */

static int g_ok = 0;
static int g_fail = 0;

typedef struct {
    int calls;
    int band;
    size_t len;
    uint8_t first_byte;
    TxResult result;
} FakeSender;

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

static TxResult fake_send(uint8_t *payload, size_t len, int band, void *ctx)
{
    FakeSender *sender = (FakeSender *)ctx;

    sender->calls++;
    sender->band = band;
    sender->len = len;
    sender->first_byte = len > 0 ? payload[0] : 0;

    return sender->result;
}

static void test_execute_success(void)
{
    DaemonTxJob job;
    DaemonTxJobResult result;
    FakeSender sender = {0, 0, 0, 0, TX_RESULT_OK};
    const uint8_t payload[] = { 0x42, 0x43 };

    daemon_tx_job_init(&job, 433, 0, 17);
    job.flags = FRAMED_DATA_TX_RESULT_FLAG_MANAGED;
    expect_int("executor payload set",
               daemon_tx_job_set_payload(&job, payload, sizeof(payload)), 0);

    result = daemon_tx_execute_job_with_sender(&job, fake_send, &sender);

    expect_int("executor calls sender", sender.calls, 1);
    expect_int("executor passes band", sender.band, 433);
    expect_size("executor passes len", sender.len, sizeof(payload));
    expect_int("executor passes payload", sender.first_byte, 0x42);
    expect_int("executor result seq", result.seq, 17);
    expect_int("executor result flags", result.flags, FRAMED_DATA_TX_RESULT_FLAG_MANAGED);
    expect_int("executor result outcome", result.outcome, DAEMON_TX_OUTCOME_OK);
    expect_int("executor tx result ok", result.tx_result, TX_RESULT_OK);
    expect_int("executor framed ok", result.framed_status, FRAMED_DATA_TX_STATUS_OK);
}

static void test_execute_busy_mapping(void)
{
    DaemonTxJob job;
    DaemonTxJobResult result;
    FakeSender sender = {0, 0, 0, 0, TX_RESULT_CAD_TIMEOUT};

    daemon_tx_job_init(&job, 868, 1, 22);

    result = daemon_tx_execute_job_with_sender(&job, fake_send, &sender);

    expect_int("executor busy sender called", sender.calls, 1);
    expect_int("executor busy outcome", result.outcome, DAEMON_TX_OUTCOME_CHANNEL_BUSY);
    expect_int("executor busy tx result", result.tx_result, TX_RESULT_CAD_TIMEOUT);
    expect_int("executor busy framed", result.framed_status,
               FRAMED_DATA_TX_STATUS_CHANNEL_BUSY);
}

static void test_execute_not_ready_mapping(void)
{
    DaemonTxJob job;
    DaemonTxJobResult result;
    FakeSender sender = {0, 0, 0, 0, TX_RESULT_RADIO_NOT_READY};

    daemon_tx_job_init(&job, 433, 0, 5);

    result = daemon_tx_execute_job_with_sender(&job, fake_send, &sender);

    expect_int("executor not ready outcome", result.outcome,
               DAEMON_TX_OUTCOME_RADIO_NOT_READY);
    expect_int("executor not ready tx result", result.tx_result,
               TX_RESULT_RADIO_NOT_READY);
    expect_int("executor not ready framed", result.framed_status,
               FRAMED_DATA_TX_STATUS_RADIO_NOT_READY);
}

static void test_execute_null_safe(void)
{
    DaemonTxJob job;
    DaemonTxJobResult result;

    daemon_tx_job_init(&job, 433, 0, 9);

    result = daemon_tx_execute_job_with_sender(NULL, fake_send, NULL);
    expect_int("executor null job outcome", result.outcome,
               DAEMON_TX_OUTCOME_INVALID_PACKET);
    expect_int("executor null job tx result", result.tx_result,
               TX_RESULT_INVALID_PACKET);
    expect_int("executor null job framed", result.framed_status,
               FRAMED_DATA_TX_STATUS_INVALID_PACKET);

    result = daemon_tx_execute_job_with_sender(&job, NULL, NULL);
    expect_int("executor null sender seq", result.seq, 9);
    expect_int("executor null sender outcome", result.outcome,
               DAEMON_TX_OUTCOME_RADIO_ERROR);
    expect_int("executor null sender tx result", result.tx_result,
               TX_RESULT_RADIO_ERROR);
    expect_int("executor null sender framed", result.framed_status,
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

    test_execute_success();
    test_execute_busy_mapping();
    test_execute_not_ready_mapping();
    test_execute_null_safe();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
