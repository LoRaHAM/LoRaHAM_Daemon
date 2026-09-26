#include "daemon_tx_async_runtime.h"

#include <atomic>

#include "daemon_log.h"

/* --- TX-Async-Runtime ---------------------------------------------------- */

struct DaemonTxAsyncCompletionRecordCtx {
    DaemonTxCompletionQueue *completion_queue;
    std::atomic<DaemonRadioStats *> stats;

    DaemonTxAsyncCompletionRecordCtx() :
        completion_queue(NULL),
        stats(NULL)
    {
    }
};

DaemonTxAsyncWorker daemon_tx_async_worker;
DaemonTxCompletionQueue daemon_tx_async_completion_queue;
static size_t daemon_tx_async_completion_stale;
static DaemonTxAsyncCompletionRecordCtx daemon_tx_async_completion_record;

void daemon_tx_async_runtime_init(void)
{
    daemon_debug_ctx("TXASYNC", "initialising the worker");

    daemon_tx_async_worker_init(&daemon_tx_async_worker);
    daemon_tx_completion_queue_init(&daemon_tx_async_completion_queue);
    daemon_tx_async_completion_record.completion_queue =
        &daemon_tx_async_completion_queue;
    daemon_tx_async_completion_record.stats.store(
        NULL, std::memory_order_release);
    daemon_tx_async_completion_stale = 0;
}

void daemon_tx_async_runtime_shutdown(void)
{
    daemon_debug_ctx("TXASYNC", "stopping the worker");

    daemon_tx_async_worker_stop(&daemon_tx_async_worker);

    daemon_tx_async_worker_init(&daemon_tx_async_worker);
    daemon_tx_async_completion_record.stats.store(
        NULL, std::memory_order_release);
}

DaemonTxAsyncWorker *daemon_tx_async_runtime_worker(void)
{
    return &daemon_tx_async_worker;
}

DaemonTxCompletionQueue *daemon_tx_async_runtime_completion_queue(void)
{
    return &daemon_tx_async_completion_queue;
}

void daemon_tx_async_runtime_set_stats(DaemonRadioStats *stats)
{
    daemon_tx_async_completion_record.stats.store(stats,
                                                  std::memory_order_release);
}

void *daemon_tx_async_runtime_completion_record_ctx(void)
{
    return &daemon_tx_async_completion_record;
}

void daemon_tx_async_runtime_record_completion_stale(void)
{
    daemon_tx_async_completion_stale++;
}

size_t daemon_tx_async_runtime_completion_stale(void)
{
    return daemon_tx_async_completion_stale;
}

void daemon_tx_async_runtime_record_completion(const DaemonTxJobResult *result,
                                               void *ctx)
{
    DaemonTxAsyncCompletionRecordCtx *record =
        (DaemonTxAsyncCompletionRecordCtx *)ctx;

    if (!record || !result)
        return;

    DaemonRadioStats *stats =
        record->stats.load(std::memory_order_acquire);

    if (stats) {
        daemon_radio_stats_record_tx_result(stats, result->tx_result);
        if ((result->flags & FRAMED_DATA_TX_RESULT_FLAG_CAD_TIMEOUT) &&
            result->tx_result == TX_RESULT_OK) {
            daemon_radio_stats_record_cad_timeout_send(stats);
        }
    }

    daemon_tx_completion_queue_push(record->completion_queue, result);
}

size_t daemon_tx_async_runtime_completion_pending(void)
{
    return daemon_tx_completion_queue_pending(&daemon_tx_async_completion_queue);
}

size_t daemon_tx_async_runtime_completion_dropped(void)
{
    return daemon_tx_completion_queue_dropped(&daemon_tx_async_completion_queue);
}

int daemon_tx_async_runtime_pop_completion(DaemonTxJobResult *out)
{
    return daemon_tx_completion_queue_pop(&daemon_tx_async_completion_queue, out);
}

size_t daemon_tx_async_runtime_pending(void)
{
    return daemon_tx_async_worker_pending(&daemon_tx_async_worker);
}

int daemon_tx_async_runtime_job_active(void)
{
    return daemon_tx_async_worker.job_active.load() ? 1 : 0;
}

size_t daemon_tx_async_runtime_accepted(void)
{
    return daemon_tx_async_worker_accepted(&daemon_tx_async_worker);
}

size_t daemon_tx_async_runtime_rejected(void)
{
    return daemon_tx_async_worker_rejected(&daemon_tx_async_worker);
}

size_t daemon_tx_async_runtime_processed(void)
{
    return daemon_tx_async_worker_processed(&daemon_tx_async_worker);
}

size_t daemon_tx_async_runtime_dropped(void)
{
    return daemon_tx_async_worker_dropped(&daemon_tx_async_worker);
}

int daemon_tx_async_runtime_last_result(DaemonTxJobResult *out)
{
    return daemon_tx_async_worker_last_result_copy(&daemon_tx_async_worker, out);
}

int daemon_tx_async_runtime_running(void)
{
    return daemon_tx_async_worker_running(&daemon_tx_async_worker);
}
