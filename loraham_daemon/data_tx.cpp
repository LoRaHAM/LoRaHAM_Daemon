#include "data_tx.h"

#include "client_slot.h"

#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

/* --- DATA TX chunking ---------------------------------------------------- */
// Rohdaten vom DATA-Socket in LoRa-Pakete zerteilen.

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


void data_tx_process_slots(const char *tag,
                           ClientSlot *slots,
                           int max_clients,
                           const EventLoopReadySet *readfds,
                           DataTxChunkHandler handler,
                           void *ctx,
                           DataTxLog log)
{
    (void)tag;

    if (!slots)
        return;

    for(int i=0;i<max_clients;i++){
        ClientSlot *slot = &slots[i];

        if(client_slot_ready(slot, readfds)) {
            uint8_t large_buf[2048];
            ssize_t n;

            do {
                n = read(slot->fd, large_buf, sizeof(large_buf));
            } while(n < 0 && errno == EINTR);

            if(n < 0) {
                if(errno == EAGAIN || errno == EWOULDBLOCK)
                    continue;

                data_tx_log_message(&log, "Lesefehler, Client zu");
                client_slot_close(slot);
                continue;
            }

            if(n == 0) {
                data_tx_log_message(&log, "EOF, Client zu");
                client_slot_close(slot);
                continue;
            }

            data_tx_log_bytes(&log, n);

            size_t processed = data_tx_for_each_chunk(large_buf, (size_t)n, handler, ctx);
            if (processed < (size_t)n)
                data_tx_log_processed(&log, processed, n);
        }
    }
}

