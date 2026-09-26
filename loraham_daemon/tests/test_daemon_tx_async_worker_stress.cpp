#include "../daemon_tx_async_worker.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <stdio.h>
#include <string.h>
#include <thread>
#include <vector>

/* --- TX-Async-Stresstest -------------------------------------------------- */

static int g_ok = 0;
static int g_fail = 0;

TxResult lora_send(uint8_t *buf, size_t len, int band)
{
    (void)band;
    (void)buf;
    (void)len;
    return TX_RESULT_RADIO_ERROR;
}

typedef struct {
    std::atomic<size_t> sends;
    std::atomic<size_t> results;
} StressState;

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

static TxResult stress_send(uint8_t *payload, size_t len, int band, void *ctx)
{
    (void)band;
    StressState *state = (StressState *)ctx;

    (void)payload;
    (void)len;

    state->sends.fetch_add(1, std::memory_order_relaxed);
    return TX_RESULT_OK;
}

static void stress_result(const DaemonTxJobResult *result, void *ctx)
{
    StressState *state = (StressState *)ctx;

    (void)result;
    state->results.fetch_add(1, std::memory_order_relaxed);
}

static DaemonTxJob make_job(uint16_t seq)
{
    DaemonTxJob job;
    uint8_t payload[] = { 0x5a };

    daemon_tx_job_init(&job, 433, 0, seq);
    daemon_tx_job_set_payload(&job, payload, sizeof(payload));

    return job;
}

static int wait_stop_requested(DaemonTxAsyncWorker *async)
{
    for (int i = 0; i < 1000; i++) {
        {
            std::lock_guard<std::mutex> guard(async->lock);

            if (async->stop_requested)
                return 1;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return 0;
}

static void test_multi_producer_stop_race(void)
{
    enum {
        STRESS_ROUNDS = 8,
        PRODUCER_COUNT = 4,
        SUBMITS_BEFORE_STOP = 32,
        SUBMITS_AFTER_STOP = 96
    };

    for (int round = 0; round < STRESS_ROUNDS; round++) {
        DaemonTxAsyncWorker async;
        StressState state;
        std::atomic<int> ready(0);
        std::atomic<bool> release_after_stop(false);
        std::atomic<size_t> submitted(0);
        std::atomic<size_t> accepted(0);
        std::atomic<size_t> rejected(0);
        std::atomic<size_t> post_stop_rejected(0);
        std::vector<std::thread> producers;

        state.sends.store(0, std::memory_order_relaxed);
        state.results.store(0, std::memory_order_relaxed);

        daemon_tx_async_worker_init(&async);
        daemon_tx_async_worker_configure(&async,
                                         stress_send,
                                         &state,
                                         stress_result,
                                         &state);

        expect_int("stress start", daemon_tx_async_worker_start(&async), 0);

        for (int producer = 0; producer < PRODUCER_COUNT; producer++) {
            producers.emplace_back([&async, &ready, &release_after_stop,
                                    &submitted, &accepted, &rejected,
                                    &post_stop_rejected, producer, round]() {
                for (int i = 0; i < SUBMITS_BEFORE_STOP; i++) {
                    DaemonTxJob job = make_job(
                        (uint16_t)(round * 1024 + producer * 128 + i));
                    int rc = daemon_tx_async_worker_submit(&async, &job);

                    submitted.fetch_add(1, std::memory_order_relaxed);
                    if (rc == 0)
                        accepted.fetch_add(1, std::memory_order_relaxed);
                    else
                        rejected.fetch_add(1, std::memory_order_relaxed);
                }

                ready.fetch_add(1, std::memory_order_release);
                while (!release_after_stop.load(std::memory_order_acquire))
                    std::this_thread::yield();

                for (int i = 0; i < SUBMITS_AFTER_STOP; i++) {
                    DaemonTxJob job = make_job(
                        (uint16_t)(round * 1024 + producer * 128 +
                                   SUBMITS_BEFORE_STOP + i));
                    int rc = daemon_tx_async_worker_submit(&async, &job);

                    submitted.fetch_add(1, std::memory_order_relaxed);
                    if (rc == 0)
                        accepted.fetch_add(1, std::memory_order_relaxed);
                    else {
                        rejected.fetch_add(1, std::memory_order_relaxed);
                        post_stop_rejected.fetch_add(
                            1, std::memory_order_relaxed);
                    }
                }
            });
        }

        std::thread stopper([&async, &ready]() {
            while (ready.load(std::memory_order_acquire) < PRODUCER_COUNT)
                std::this_thread::yield();

            daemon_tx_async_worker_stop(&async);
        });

        expect_int("stress stop requested", wait_stop_requested(&async), 1);
        release_after_stop.store(true, std::memory_order_release);

        for (std::thread& producer : producers)
            producer.join();
        stopper.join();

        size_t expected_submits =
            (size_t)PRODUCER_COUNT *
            (SUBMITS_BEFORE_STOP + SUBMITS_AFTER_STOP);
        size_t worker_accepted = daemon_tx_async_worker_accepted(&async);
        size_t worker_rejected = daemon_tx_async_worker_rejected(&async);
        size_t processed = daemon_tx_async_worker_processed(&async);
        size_t dropped = daemon_tx_async_worker_dropped(&async);

        expect_size("stress attempts accounted",
                    submitted.load(std::memory_order_relaxed),
                    expected_submits);
        expect_size("stress local accounting",
                    accepted.load(std::memory_order_relaxed) +
                    rejected.load(std::memory_order_relaxed),
                    expected_submits);
        expect_size("stress accepted counter", worker_accepted,
                    accepted.load(std::memory_order_relaxed));
        expect_size("stress rejected counter", worker_rejected,
                    rejected.load(std::memory_order_relaxed));
        expect_size("stress post-stop rejected",
                    post_stop_rejected.load(std::memory_order_relaxed),
                    (size_t)PRODUCER_COUNT * SUBMITS_AFTER_STOP);
        expect_int("stress stopped",
                   daemon_tx_async_worker_running(&async), 0);
        expect_size("stress pending empty",
                    daemon_tx_async_worker_pending(&async), 0);
        expect_size("stress send count",
                    state.sends.load(std::memory_order_relaxed),
                    processed);
        expect_size("stress result count",
                    state.results.load(std::memory_order_relaxed),
                    processed);
        expect_int("stress processed bounded", processed <= worker_accepted, 1);
        expect_int("stress discard accounted",
                   dropped >= worker_accepted - processed, 1);
    }
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

    test_multi_producer_stop_race();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
