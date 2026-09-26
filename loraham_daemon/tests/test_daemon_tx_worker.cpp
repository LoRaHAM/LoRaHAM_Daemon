#include "../daemon_tx_worker.h"

#include <stdio.h>
#include <string.h>

/* --- TX worker facade tests --------------------------------------------- */

static int g_ok = 0;
static int g_fail = 0;

typedef struct {
    int calls;
    TxResult result[16];
} FakeSender;

typedef struct {
    int calls;
    uint16_t seq[16];
    TxResult tx_result[16];
    uint8_t framed_status[16];
} ResultRecorder;

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
    (void)band;
    FakeSender *sender = (FakeSender *)ctx;
    int idx = sender->calls;

    (void)payload;
    (void)len;

    sender->calls++;
    return sender->result[idx];
}

static void record_result(const DaemonTxJobResult *result, void *ctx)
{
    ResultRecorder *rec = (ResultRecorder *)ctx;
    int idx = rec->calls;

    rec->seq[idx] = result->seq;
    rec->tx_result[idx] = result->tx_result;
    rec->framed_status[idx] = result->framed_status;
    rec->calls++;
}

static DaemonTxJob make_job(int band, uint16_t seq, uint8_t byte)
{
    DaemonTxJob job;
    uint8_t payload[] = { byte };

    daemon_tx_job_init(&job, band, 0, seq);
    daemon_tx_job_set_payload(&job, payload, sizeof(payload));

    return job;
}

static void test_init_submit_and_counters(void)
{
    DaemonTxWorker worker;
    DaemonTxJob job = make_job(433, 1, 0x41);

    daemon_tx_worker_init(&worker);

    expect_size("worker pending zero", daemon_tx_worker_pending(&worker), 0);
    expect_size("worker accepted zero", daemon_tx_worker_accepted(&worker), 0);
    expect_size("worker rejected zero", daemon_tx_worker_rejected(&worker), 0);
    expect_size("worker processed zero", daemon_tx_worker_processed(&worker), 0);
    expect_int("worker no last result", daemon_tx_worker_last_result(&worker) == NULL, 1);

    expect_int("worker submit ok", daemon_tx_worker_submit(&worker, &job), 0);
    expect_size("worker pending one", daemon_tx_worker_pending(&worker), 1);
    expect_size("worker accepted one", daemon_tx_worker_accepted(&worker), 1);
    expect_size("worker rejected still zero", daemon_tx_worker_rejected(&worker), 0);
}

static void test_full_rejects_and_counts(void)
{
    DaemonTxWorker worker;

    daemon_tx_worker_init(&worker);

    for (int i = 0; i < DAEMON_TX_QUEUE_CAPACITY; i++) {
        DaemonTxJob job = make_job(433, (uint16_t)(i + 1), (uint8_t)i);
        expect_int("worker fill submit", daemon_tx_worker_submit(&worker, &job), 0);
    }

    DaemonTxJob extra = make_job(868, 77, 0x77);
    expect_int("worker rejects full", daemon_tx_worker_submit(&worker, &extra), -1);
    expect_size("worker accepted capacity", daemon_tx_worker_accepted(&worker),
                DAEMON_TX_QUEUE_CAPACITY);
    expect_size("worker rejected one", daemon_tx_worker_rejected(&worker), 1);
    expect_size("worker dropped zero", daemon_tx_worker_dropped(&worker), 0);
}

static void test_drain_updates_worker_state(void)
{
    DaemonTxWorker worker;
    FakeSender sender;
    ResultRecorder rec;
    const DaemonTxJobResult *last;

    memset(&sender, 0, sizeof(sender));
    memset(&rec, 0, sizeof(rec));
    sender.result[0] = TX_RESULT_OK;
    sender.result[1] = TX_RESULT_CAD_TIMEOUT;
    sender.result[2] = TX_RESULT_RADIO_NOT_READY;

    daemon_tx_worker_init(&worker);

    DaemonTxJob job0 = make_job(433, 10, 0x10);
    DaemonTxJob job1 = make_job(868, 11, 0x11);
    DaemonTxJob job2 = make_job(433, 12, 0x12);
    daemon_tx_worker_submit(&worker, &job0);
    daemon_tx_worker_submit(&worker, &job1);
    daemon_tx_worker_submit(&worker, &job2);

    expect_size("worker drain all",
                daemon_tx_worker_drain(&worker, fake_send, &sender,
                                       record_result, &rec, 0),
                3);
    expect_int("worker sender calls", sender.calls, 3);
    expect_int("worker result callbacks", rec.calls, 3);
    expect_size("worker processed three", daemon_tx_worker_processed(&worker), 3);
    expect_size("worker pending empty", daemon_tx_worker_pending(&worker), 0);

    expect_int("worker second framed busy", rec.framed_status[1],
               FRAMED_DATA_TX_STATUS_CHANNEL_BUSY);
    expect_int("worker third tx result", rec.tx_result[2], TX_RESULT_RADIO_NOT_READY);

    last = daemon_tx_worker_last_result(&worker);
    expect_int("worker has last result", last != NULL, 1);
    if (last) {
        expect_int("worker last seq", last->seq, 12);
        expect_int("worker last tx result", last->tx_result, TX_RESULT_RADIO_NOT_READY);
    }
}

static void test_drain_limited_preserves_remainder(void)
{
    DaemonTxWorker worker;
    FakeSender sender;
    ResultRecorder rec;

    memset(&sender, 0, sizeof(sender));
    memset(&rec, 0, sizeof(rec));
    sender.result[0] = TX_RESULT_OK;
    sender.result[1] = TX_RESULT_OK;

    daemon_tx_worker_init(&worker);

    DaemonTxJob job0 = make_job(433, 20, 0x20);
    DaemonTxJob job1 = make_job(433, 21, 0x21);
    daemon_tx_worker_submit(&worker, &job0);
    daemon_tx_worker_submit(&worker, &job1);

    expect_size("worker drain one",
                daemon_tx_worker_drain(&worker, fake_send, &sender,
                                       record_result, &rec, 1),
                1);
    expect_size("worker processed one", daemon_tx_worker_processed(&worker), 1);
    expect_size("worker leaves one", daemon_tx_worker_pending(&worker), 1);
}

static void test_null_safe(void)
{
    DaemonTxWorker worker;

    daemon_tx_worker_init(NULL);
    expect_size("worker null pending", daemon_tx_worker_pending(NULL), 0);
    expect_size("worker null accepted", daemon_tx_worker_accepted(NULL), 0);
    expect_size("worker null rejected", daemon_tx_worker_rejected(NULL), 0);
    expect_size("worker null processed", daemon_tx_worker_processed(NULL), 0);
    expect_int("worker null last", daemon_tx_worker_last_result(NULL) == NULL, 1);
    expect_int("worker null submit", daemon_tx_worker_submit(NULL, NULL), -1);
    expect_size("worker null drain", daemon_tx_worker_drain(NULL, NULL, NULL,
                                                            NULL, NULL, 0), 0);

    daemon_tx_worker_init(&worker);
    expect_int("worker null job rejected", daemon_tx_worker_submit(&worker, NULL), -1);
    expect_size("worker null job rejected count", daemon_tx_worker_rejected(&worker), 1);
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

    test_init_submit_and_counters();
    test_full_rejects_and_counts();
    test_drain_updates_worker_state();
    test_drain_limited_preserves_remainder();
    test_null_safe();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
