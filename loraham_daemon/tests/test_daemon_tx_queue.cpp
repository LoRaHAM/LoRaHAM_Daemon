#include "../daemon_tx_queue.h"

#include <stdio.h>
#include <string.h>

/* --- TX queue/drain seam tests ----------------------------------------- */

static int g_ok = 0;
static int g_fail = 0;

typedef struct {
    int calls;
    int band[16];
    size_t len[16];
    TxResult result[16];
} FakeSender;

typedef struct {
    int calls;
    uint16_t seq[16];
    DaemonTxOutcome outcome[16];
    uint8_t framed_status[16];
    TxResult tx_result[16];
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
    FakeSender *sender = (FakeSender *)ctx;
    int idx = sender->calls;

    (void)payload;

    sender->band[idx] = band;
    sender->len[idx] = len;
    sender->calls++;

    return sender->result[idx];
}

static void record_result(const DaemonTxJobResult *result, void *ctx)
{
    ResultRecorder *rec = (ResultRecorder *)ctx;
    int idx = rec->calls;

    rec->seq[idx] = result->seq;
    rec->outcome[idx] = result->outcome;
    rec->framed_status[idx] = result->framed_status;
    rec->tx_result[idx] = result->tx_result;
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

static void test_init_and_empty(void)
{
    DaemonTxQueue queue;

    daemon_tx_queue_init(&queue);

    expect_int("queue empty", daemon_tx_queue_empty(&queue), 1);
    expect_int("queue not full", daemon_tx_queue_full(&queue), 0);
    expect_size("queue count zero", daemon_tx_queue_count(&queue), 0);
    expect_size("queue dropped zero", daemon_tx_queue_dropped(&queue), 0);
}

static void test_push_pop_order(void)
{
    DaemonTxQueue queue;
    DaemonTxJob a = make_job(433, 1, 0x11);
    DaemonTxJob b = make_job(868, 2, 0x22);
    DaemonTxJob out = {};

    daemon_tx_queue_init(&queue);

    expect_int("push a", daemon_tx_queue_push(&queue, &a), 0);
    expect_int("push b", daemon_tx_queue_push(&queue, &b), 0);
    expect_size("count after push", daemon_tx_queue_count(&queue), 2);

    expect_int("pop a", daemon_tx_queue_pop(&queue, &out), 0);
    expect_int("pop a seq", out.seq, 1);
    expect_int("pop a band", out.band, 433);

    expect_int("pop b", daemon_tx_queue_pop(&queue, &out), 0);
    expect_int("pop b seq", out.seq, 2);
    expect_int("pop b band", out.band, 868);
    expect_int("empty after pop", daemon_tx_queue_empty(&queue), 1);
}

static void test_full_rejects_newest(void)
{
    DaemonTxQueue queue;
    DaemonTxJob out = {};

    daemon_tx_queue_init(&queue);

    for (int i = 0; i < DAEMON_TX_QUEUE_CAPACITY; i++) {
        DaemonTxJob job = make_job(433, (uint16_t)(i + 1), (uint8_t)i);
        expect_int("fill push", daemon_tx_queue_push(&queue, &job), 0);
    }

    expect_int("queue full", daemon_tx_queue_full(&queue), 1);

    DaemonTxJob extra = make_job(868, 99, 0x99);
    expect_int("full rejects newest", daemon_tx_queue_push(&queue, &extra), -1);
    expect_size("dropped stays zero", daemon_tx_queue_dropped(&queue), 0);
    expect_size("count unchanged after reject", daemon_tx_queue_count(&queue),
                DAEMON_TX_QUEUE_CAPACITY);

    expect_int("pop still oldest", daemon_tx_queue_pop(&queue, &out), 0);
    expect_int("oldest seq preserved", out.seq, 1);
}

static void test_drain_all(void)
{
    DaemonTxQueue queue;
    FakeSender sender;
    ResultRecorder rec;

    memset(&sender, 0, sizeof(sender));
    memset(&rec, 0, sizeof(rec));
    sender.result[0] = TX_RESULT_OK;
    sender.result[1] = TX_RESULT_CAD_TIMEOUT;
    sender.result[2] = TX_RESULT_RADIO_NOT_READY;

    daemon_tx_queue_init(&queue);
    DaemonTxJob job0 = make_job(433, 10, 0x10);
    DaemonTxJob job1 = make_job(868, 11, 0x11);
    DaemonTxJob job2 = make_job(433, 12, 0x12);
    daemon_tx_queue_push(&queue, &job0);
    daemon_tx_queue_push(&queue, &job1);
    daemon_tx_queue_push(&queue, &job2);

    expect_size("drain all count",
                daemon_tx_queue_drain(&queue, fake_send, &sender,
                                      record_result, &rec, 0),
                3);
    expect_int("drain sender calls", sender.calls, 3);
    expect_int("drain result calls", rec.calls, 3);
    expect_int("drain first seq", rec.seq[0], 10);
    expect_int("drain first outcome", rec.outcome[0], DAEMON_TX_OUTCOME_OK);
    expect_int("drain second outcome", rec.outcome[1], DAEMON_TX_OUTCOME_CHANNEL_BUSY);
    expect_int("drain second framed", rec.framed_status[1],
               FRAMED_DATA_TX_STATUS_CHANNEL_BUSY);
    expect_int("drain third tx result", rec.tx_result[2], TX_RESULT_RADIO_NOT_READY);
    expect_int("drain queue empty", daemon_tx_queue_empty(&queue), 1);
}

static void test_drain_limited(void)
{
    DaemonTxQueue queue;
    FakeSender sender;
    ResultRecorder rec;

    memset(&sender, 0, sizeof(sender));
    memset(&rec, 0, sizeof(rec));
    sender.result[0] = TX_RESULT_OK;
    sender.result[1] = TX_RESULT_OK;

    daemon_tx_queue_init(&queue);
    DaemonTxJob job0 = make_job(433, 20, 0x20);
    DaemonTxJob job1 = make_job(433, 21, 0x21);
    daemon_tx_queue_push(&queue, &job0);
    daemon_tx_queue_push(&queue, &job1);

    expect_size("drain one count",
                daemon_tx_queue_drain(&queue, fake_send, &sender,
                                      record_result, &rec, 1),
                1);
    expect_int("drain one sender calls", sender.calls, 1);
    expect_size("drain one leaves one", daemon_tx_queue_count(&queue), 1);

    DaemonTxJob out = {};
    expect_int("remaining pop", daemon_tx_queue_pop(&queue, &out), 0);
    expect_int("remaining seq", out.seq, 21);
}

static void test_null_safe(void)
{
    DaemonTxQueue queue;
    DaemonTxJob job = make_job(433, 1, 0x01);

    daemon_tx_queue_init(NULL);
    expect_size("null count", daemon_tx_queue_count(NULL), 0);
    expect_int("null empty", daemon_tx_queue_empty(NULL), 1);
    expect_int("null push", daemon_tx_queue_push(NULL, &job), -1);
    expect_int("null pop", daemon_tx_queue_pop(NULL, &job), -1);

    daemon_tx_queue_init(&queue);
    expect_int("null job push", daemon_tx_queue_push(&queue, NULL), -1);
    expect_size("null drain", daemon_tx_queue_drain(NULL, NULL, NULL, NULL, NULL, 0), 0);
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

    test_init_and_empty();
    test_push_pop_order();
    test_full_rejects_newest();
    test_drain_all();
    test_drain_limited();
    test_null_safe();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
