#ifndef LORAHAM_DATA_TX_H
#define LORAHAM_DATA_TX_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

#include "client_slot.h"
#include "event_loop.h"

/* --- DATA TX chunking --- */

#define DATA_TX_MAX_CHUNK_SIZE 255

size_t data_tx_chunk_size(size_t remaining);

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

    snprintf(msg, sizeof(msg), "%zd Byte empfangen", n);
    data_tx_log_message(log, msg);
}

static inline void data_tx_log_processed(const DataTxLog *log,
                                         size_t processed,
                                         ssize_t total)
{
    char msg[96];

    snprintf(msg, sizeof(msg), "Abbruch nach %zu/%zd Byte", processed, total);
    data_tx_log_message(log, msg);
}

size_t data_tx_for_each_chunk(uint8_t *buf,
                              size_t len,
                              DataTxChunkHandler handler,
                              void *ctx);

void data_tx_process_slots(const char *tag,
                           ClientSlot *slots,
                           int max_clients,
                           const EventLoopReadySet *readfds,
                           DataTxChunkHandler handler,
                           void *ctx,
                           DataTxLog log);



#endif
