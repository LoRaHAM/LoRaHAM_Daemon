#ifndef LORAHAM_DAEMON_TX_ASYNC_WORKER_H
#define LORAHAM_DAEMON_TX_ASYNC_WORKER_H

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "daemon_tx_worker.h"

/* --- TX-Async-Worker ----------------------------------------------------- */

struct DaemonTxAsyncWorker {
    DaemonTxWorker worker;
    DaemonTxSendFn send_fn;
    void *send_ctx;
    DaemonTxCadProbeFn cad_probe_fn;
    void *cad_probe_ctx;
    DaemonTxSleepFn cad_sleep_fn;
    void *cad_sleep_ctx;
    DaemonTxWorkerResultFn result_fn;
    void *result_ctx;
    std::mutex lock;
    std::condition_variable wake;
    std::thread thread;
    bool running;
    bool stop_requested;
    /* True while a popped job executes: pending-count alone
     * has a pop-to-transmit window in which a CONFIG change could still
     * retune an already-accepted job. */
    std::atomic<bool> job_active{false};

    DaemonTxAsyncWorker() :
        send_fn(NULL),
        send_ctx(NULL),
        cad_probe_fn(NULL),
        cad_probe_ctx(NULL),
        cad_sleep_fn(NULL),
        cad_sleep_ctx(NULL),
        result_fn(NULL),
        result_ctx(NULL),
        running(false),
        stop_requested(false)
    {
        daemon_tx_worker_init(&worker);
    }
};

void daemon_tx_async_worker_init(DaemonTxAsyncWorker *async);

void daemon_tx_async_worker_configure(DaemonTxAsyncWorker *async,
                                                    DaemonTxSendFn send_fn,
                                                    void *send_ctx,
                                                    DaemonTxWorkerResultFn result_fn,
                                                    void *result_ctx);

void daemon_tx_async_worker_configure_cad(DaemonTxAsyncWorker *async,
                                                         DaemonTxCadProbeFn cad_probe_fn,
                                                         void *cad_probe_ctx,
                                                         DaemonTxSleepFn cad_sleep_fn,
                                                         void *cad_sleep_ctx);

size_t daemon_tx_async_worker_pending(DaemonTxAsyncWorker *async);

size_t daemon_tx_async_worker_accepted(DaemonTxAsyncWorker *async);

size_t daemon_tx_async_worker_processed(DaemonTxAsyncWorker *async);

size_t daemon_tx_async_worker_rejected(DaemonTxAsyncWorker *async);

size_t daemon_tx_async_worker_dropped(DaemonTxAsyncWorker *async);

int daemon_tx_async_worker_running(DaemonTxAsyncWorker *async);

static inline const DaemonTxJobResult *daemon_tx_async_worker_last_result_unsafe(DaemonTxAsyncWorker *async)
{
    return async ? daemon_tx_worker_last_result(&async->worker) : NULL;
}

int daemon_tx_async_worker_last_result_copy(DaemonTxAsyncWorker *async,
                                                             DaemonTxJobResult *out);

void daemon_tx_async_worker_loop(DaemonTxAsyncWorker *async);

int daemon_tx_async_worker_start(DaemonTxAsyncWorker *async);

void daemon_tx_async_worker_stop(DaemonTxAsyncWorker *async);

int daemon_tx_async_worker_submit(DaemonTxAsyncWorker *async,
                                                const DaemonTxJob *job);

#endif
