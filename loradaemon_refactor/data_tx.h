#ifndef LORAHAM_DATA_TX_H
#define LORAHAM_DATA_TX_H

#include <stddef.h>
#include <stdint.h>

#include "event_loop.h"

/* --- DATA TX chunking --- */

#define DATA_TX_MAX_CHUNK_SIZE 255

size_t data_tx_chunk_size(size_t remaining);

typedef int (*DataTxChunkHandler)(uint8_t *chunk,
                                  size_t len,
                                  size_t offset,
                                  void *ctx);

size_t data_tx_for_each_chunk(uint8_t *buf,
                              size_t len,
                              DataTxChunkHandler handler,
                              void *ctx);

void data_tx_process_clients(const char *tag,
                             int *clients,
                             int max_clients,
                             const EventLoopReadySet *readfds,
                             DataTxChunkHandler handler,
                             void *ctx);

#endif
