#ifndef LORAHAM_DATA_TX_H
#define LORAHAM_DATA_TX_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

#include "client_slot.h"
#include "event_loop.h"

/* --- DATA TX chunking --- */

/* Absolute ceiling of the chunker, not the radio's answer: what the radio will
 * accept right now comes from radio_tx_payload_limit() and is passed in. */
#define DATA_TX_MAX_CHUNK_SIZE 255

size_t data_tx_chunk_size(size_t remaining, size_t limit);

typedef int (*DataTxChunkHandler)(uint8_t *chunk,
                                  size_t len,
                                  size_t offset,
                                  void *ctx);

typedef void (*DataTxLogFn)(void *ctx, const char *msg);

typedef struct {
    void *ctx;
    DataTxLogFn message;
} DataTxLog;

static inline void data_tx_log_message(const DataTxLog *log, const char *msg)
{
    if (log && log->message)
        log->message(log->ctx, msg);
}

static inline void data_tx_log_bytes(const DataTxLog *log, ssize_t n)
{
    char msg[64];

    snprintf(msg, sizeof(msg), "%zd bytes received", n);
    data_tx_log_message(log, msg);
}

static inline void data_tx_log_processed(const DataTxLog *log,
                                         size_t processed,
                                         ssize_t total)
{
    char msg[96];

    snprintf(msg, sizeof(msg), "aborted after %zu/%zd bytes", processed, total);
    data_tx_log_message(log, msg);
}

size_t data_tx_for_each_chunk(uint8_t *buf,
                              size_t len,
                              size_t limit,
                              DataTxChunkHandler handler,
                              void *ctx);

/* chunk_limit_fn: the largest payload the radio will accept RIGHT NOW,
 * queried per read. NULL = DATA_TX_MAX_CHUNK_SIZE. This and
 * capacity_bytes_fn must agree on the same limit: if chunks shrink to 63
 * while the capacity bound still counts 255-byte slots, one read consumes
 * about four times what the free slots can hold and the chunker generates
 * more jobs than there is room for -- which is exactly what the "leave the
 * excess in the kernel rather than drop it" design exists to prevent. */
typedef size_t (*DataTxChunkLimitFn)(void *ctx);

/* capacity_bytes_fn: upper bound on bytes to consume from one
 * client read, queried per slot. NULL = unlimited. Returning 0 skips the
 * read entirely — level-triggered epoll re-delivers the readiness once
 * capacity exists, so unread bytes stay in the kernel buffer instead of
 * being chunked into a full queue and dropped. */
typedef size_t (*DataTxCapacityFn)(void *ctx);

void data_tx_process_slots(const char *tag,
                           ClientSlot *slots,
                           int max_clients,
                           const EventLoopReadySet *readfds,
                           DataTxChunkHandler handler,
                           void *ctx,
                           DataTxLog log,
                           DataTxCapacityFn capacity_bytes_fn,
                           DataTxChunkLimitFn chunk_limit_fn);



#endif
