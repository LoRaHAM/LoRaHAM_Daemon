#include "daemon_tx_queue.h"

/* Bodies moved verbatim from daemon_tx_queue.h. */

void daemon_tx_queue_init(DaemonTxQueue *queue)
{
    if (!queue)
        return;

    memset(queue, 0, sizeof(*queue));
}

int daemon_tx_queue_push(DaemonTxQueue *queue,
                                       const DaemonTxJob *job)
{
    if (!queue || !job)
        return -1;

    if (daemon_tx_queue_full(queue)) {
        /* Full: the submission is refused; no packet is discarded. */
        return -1;
    }

    queue->jobs[queue->tail] = *job;
    queue->tail = (queue->tail + 1) % DAEMON_TX_QUEUE_CAPACITY;
    queue->count++;

    return 0;
}

int daemon_tx_queue_pop(DaemonTxQueue *queue,
                                      DaemonTxJob *job)
{
    if (!queue || !job || queue->count == 0)
        return -1;

    *job = queue->jobs[queue->head];
    queue->head = (queue->head + 1) % DAEMON_TX_QUEUE_CAPACITY;
    queue->count--;

    return 0;
}

size_t daemon_tx_queue_discard_all(DaemonTxQueue *queue)
{
    size_t discarded;

    if (!queue)
        return 0;

    discarded = queue->count;
    memset(queue->jobs, 0, sizeof(queue->jobs));
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    queue->dropped += discarded;

    return discarded;
}

size_t daemon_tx_queue_drain(DaemonTxQueue *queue,
                                           DaemonTxSendFn send_fn,
                                           void *send_ctx,
                                           DaemonTxQueueResultFn result_fn,
                                           void *result_ctx,
                                           size_t max_jobs)
{
    size_t processed = 0;
    DaemonTxJob job;

    if (!queue)
        return 0;

    while (queue->count > 0 && (max_jobs == 0 || processed < max_jobs)) {
        DaemonTxJobResult result;

        if (daemon_tx_queue_pop(queue, &job) != 0)
            break;

        result = daemon_tx_execute_job_with_sender(&job, send_fn, send_ctx);

        if (result_fn)
            result_fn(&result, result_ctx);

        processed++;
    }

    return processed;
}
