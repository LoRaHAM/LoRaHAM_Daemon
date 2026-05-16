#include "data_tx.h"

/* --- DATA TX chunking --- */

size_t data_tx_chunk_size(size_t remaining)
{
    if (remaining > DATA_TX_MAX_CHUNK_SIZE)
        return DATA_TX_MAX_CHUNK_SIZE;

    return remaining;
}

size_t data_tx_for_each_chunk(uint8_t *buf,
                              size_t len,
                              DataTxChunkHandler handler,
                              void *ctx)
{
    size_t bytes_sent = 0;

    while (bytes_sent < len) {
        size_t chunk_size = data_tx_chunk_size(len - bytes_sent);

        if (chunk_size == 0)
            break;

        if (handler(buf + bytes_sent, chunk_size, bytes_sent, ctx) != 0)
            break;

        bytes_sent += chunk_size;
    }

    return bytes_sent;
}
