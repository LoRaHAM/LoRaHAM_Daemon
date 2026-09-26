#ifndef LORAHAM_DAEMON_TX_ASYNC_RUNTIME_H
#define LORAHAM_DAEMON_TX_ASYNC_RUNTIME_H

#include <stddef.h>

#include "daemon_tx_async_worker.h"
#include "daemon_tx_completion.h"
#include "daemon_stats.h"

/* --- TX-Async-Runtime ---------------------------------------------------- */
/* One band per process: one worker, one completion queue, one record ctx. */

extern DaemonTxAsyncWorker daemon_tx_async_worker;
extern DaemonTxCompletionQueue daemon_tx_async_completion_queue;

void daemon_tx_async_runtime_init(void);
void daemon_tx_async_runtime_shutdown(void);
DaemonTxAsyncWorker *daemon_tx_async_runtime_worker(void);
int daemon_tx_async_runtime_pop_completion(DaemonTxJobResult *out);
size_t daemon_tx_async_runtime_completion_dropped(void);
size_t daemon_tx_async_runtime_completion_stale(void);
void daemon_tx_async_runtime_record_completion_stale(void);
void daemon_tx_async_runtime_set_stats(DaemonRadioStats *stats);
void *daemon_tx_async_runtime_completion_record_ctx(void);
size_t daemon_tx_async_runtime_completion_pending(void);
void daemon_tx_async_runtime_record_completion(const DaemonTxJobResult *result, void *ctx);
DaemonTxCompletionQueue *daemon_tx_async_runtime_completion_queue(void);
size_t daemon_tx_async_runtime_pending(void);
/* 1 while the worker executes a popped job. */
int daemon_tx_async_runtime_job_active(void);
size_t daemon_tx_async_runtime_dropped(void);
size_t daemon_tx_async_runtime_processed(void);
size_t daemon_tx_async_runtime_rejected(void);
size_t daemon_tx_async_runtime_accepted(void);
int daemon_tx_async_runtime_last_result(DaemonTxJobResult *out);
int daemon_tx_async_runtime_running(void);

#endif
