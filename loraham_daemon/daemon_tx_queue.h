#ifndef LORAHAM_DAEMON_TX_QUEUE_H
#define LORAHAM_DAEMON_TX_QUEUE_H

#include <stddef.h>
#include <string.h>

#include "daemon_tx_executor.h"
#include "daemon_tx_job.h"

/* --- Interne TX-Warteschlange ------------------------------------------ */

#ifndef DAEMON_TX_QUEUE_CAPACITY
#define DAEMON_TX_QUEUE_CAPACITY 8
#endif

typedef void (*DaemonTxQueueResultFn)(const DaemonTxJobResult *result,
                                      void *ctx);

typedef struct {
    DaemonTxJob jobs[DAEMON_TX_QUEUE_CAPACITY];
    size_t head;
    size_t tail;
    size_t count;
    size_t dropped; /* Discarded at stop. */
} DaemonTxQueue;

void daemon_tx_queue_init(DaemonTxQueue *queue);

static inline size_t daemon_tx_queue_count(const DaemonTxQueue *queue)
{
    return queue ? queue->count : 0;
}

static inline size_t daemon_tx_queue_dropped(const DaemonTxQueue *queue)
{
    return queue ? queue->dropped : 0;
}

static inline int daemon_tx_queue_empty(const DaemonTxQueue *queue)
{
    return !queue || queue->count == 0;
}

static inline int daemon_tx_queue_full(const DaemonTxQueue *queue)
{
    return queue && queue->count >= DAEMON_TX_QUEUE_CAPACITY;
}

int daemon_tx_queue_push(DaemonTxQueue *queue,
                                       const DaemonTxJob *job);

int daemon_tx_queue_pop(DaemonTxQueue *queue,
                                      DaemonTxJob *job);


size_t daemon_tx_queue_discard_all(DaemonTxQueue *queue);

/* Testnaht: synchrones Leeren. */
size_t daemon_tx_queue_drain(DaemonTxQueue *queue,
                                           DaemonTxSendFn send_fn,
                                           void *send_ctx,
                                           DaemonTxQueueResultFn result_fn,
                                           void *result_ctx,
                                           size_t max_jobs);

#endif
