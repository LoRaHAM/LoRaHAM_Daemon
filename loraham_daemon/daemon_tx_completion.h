#ifndef LORAHAM_DAEMON_TX_COMPLETION_H
#define LORAHAM_DAEMON_TX_COMPLETION_H

#include <stddef.h>
#include <stdint.h>
#include <mutex>

#include "client_slot.h"
#include "daemon_tx_job.h"
#include "framed_data.h"

/* --- TX-Abschlussmeldung ------------------------------------------------- */

#define DAEMON_TX_COMPLETION_QUEUE_CAPACITY 16
#define DAEMON_TX_COMPLETION_DELIVERY_STALE 2

static inline size_t daemon_tx_completion_frame_len(void)
{
    return framed_data_frame_size(FRAMED_DATA_TX_RESULT_PAYLOAD_LEN);
}

int daemon_tx_completion_encode_frame(uint8_t *frame,
                                                    size_t frame_len,
                                                    const DaemonTxJobResult *result);



int daemon_tx_completion_deliver_to_slot(ClientSlot *slots,
                                                       int max_slots,
                                                       const DaemonTxJobResult *result);


/* --- TX-Abschluss-Warteschlange ------------------------------------------ */

struct DaemonTxCompletionQueue {
    std::mutex lock;
    DaemonTxJobResult entries[DAEMON_TX_COMPLETION_QUEUE_CAPACITY];
    size_t head;
    size_t count;
    size_t dropped;

    DaemonTxCompletionQueue() : head(0), count(0), dropped(0) {}
};

void daemon_tx_completion_queue_init(DaemonTxCompletionQueue *queue);

size_t daemon_tx_completion_queue_pending(DaemonTxCompletionQueue *queue);

size_t daemon_tx_completion_queue_dropped(DaemonTxCompletionQueue *queue);

int daemon_tx_completion_queue_push(DaemonTxCompletionQueue *queue,
                                                  const DaemonTxJobResult *result);

int daemon_tx_completion_queue_pop(DaemonTxCompletionQueue *queue,
                                                 DaemonTxJobResult *out);


#endif
