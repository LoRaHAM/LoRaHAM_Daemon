#ifndef LORAHAM_DAEMON_TX_WORKER_H
#define LORAHAM_DAEMON_TX_WORKER_H

#include <stddef.h>
#include <string.h>

#include "daemon_tx_queue.h"

/* --- TX-Worker-Zustand -------------------------------------------------- */

typedef void (*DaemonTxWorkerResultFn)(const DaemonTxJobResult *result,
                                       void *ctx);

typedef struct {
    DaemonTxQueue queue;
    size_t accepted;
    size_t rejected;
    size_t processed;
    int has_last_result;
    DaemonTxJobResult last_result;
} DaemonTxWorker;

typedef struct {
    DaemonTxWorker *worker;
    DaemonTxWorkerResultFn result_fn;
    void *result_ctx;
} DaemonTxWorkerDrainCtx;

void daemon_tx_worker_init(DaemonTxWorker *worker);

static inline size_t daemon_tx_worker_pending(const DaemonTxWorker *worker)
{
    return worker ? daemon_tx_queue_count(&worker->queue) : 0;
}

static inline size_t daemon_tx_worker_accepted(const DaemonTxWorker *worker)
{
    return worker ? worker->accepted : 0;
}

static inline size_t daemon_tx_worker_rejected(const DaemonTxWorker *worker)
{
    return worker ? worker->rejected : 0;
}

static inline size_t daemon_tx_worker_processed(const DaemonTxWorker *worker)
{
    return worker ? worker->processed : 0;
}

static inline size_t daemon_tx_worker_dropped(const DaemonTxWorker *worker)
{
    return worker ? daemon_tx_queue_dropped(&worker->queue) : 0;
}

const DaemonTxJobResult *daemon_tx_worker_last_result(const DaemonTxWorker *worker);

int daemon_tx_worker_submit(DaemonTxWorker *worker,
                                          const DaemonTxJob *job);

void daemon_tx_worker_record_result(const DaemonTxJobResult *result,
                                                  void *ctx);

/* Testnaht: synchrones Leeren. */
size_t daemon_tx_worker_drain(DaemonTxWorker *worker,
                                            DaemonTxSendFn send_fn,
                                            void *send_ctx,
                                            DaemonTxWorkerResultFn result_fn,
                                            void *result_ctx,
                                            size_t max_jobs);

#endif
