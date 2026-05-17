#include "data_tx.h"

#include "client_set.h"

#include <stdio.h>

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

void data_tx_process_clients(const char *tag,
                             int *clients,
                             int max_clients,
                             const EventLoopReadySet *readfds,
                             DataTxChunkHandler handler,
                             void *ctx)
{
    for(int i=0;i<max_clients;i++){
        if(client_set_slot_ready(clients, i, readfds)) {
            uint8_t large_buf[2048];
            ssize_t n = client_set_read_slot(clients, i, large_buf, sizeof(large_buf));

            if(n > 0) {
                printf("[DEBUG %s] %zd Bytes vom Socket erhalten. Zerteile in LoRa-Pakete...\n", tag, n);

                data_tx_for_each_chunk(large_buf, (size_t)n, handler, ctx);
            }
        }
    }
}
