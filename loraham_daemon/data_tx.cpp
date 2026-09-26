#include "data_tx.h"

#include "client_slot.h"

#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

/* --- DATA TX chunking ---------------------------------------------------- */
// Split raw DATA socket input into RF-sized chunks.

size_t data_tx_chunk_size(size_t remaining, size_t limit)
{
    if (limit == 0 || limit > DATA_TX_MAX_CHUNK_SIZE)
        limit = DATA_TX_MAX_CHUNK_SIZE;

    if (remaining > limit)
        return limit;

    return remaining;
}

size_t data_tx_for_each_chunk(uint8_t *buf,
                              size_t len,
                              size_t limit,
                              DataTxChunkHandler handler,
                              void *ctx)
{
    size_t bytes_sent = 0;

    while (bytes_sent < len) {
        size_t chunk_size = data_tx_chunk_size(len - bytes_sent, limit);

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
                           DataTxLog log,
                           DataTxCapacityFn capacity_bytes_fn,
                           DataTxChunkLimitFn chunk_limit_fn)
{
    (void)tag;

    if (!slots)
        return;

    for(int i=0;i<max_clients;i++){
        ClientSlot *slot = &slots[i];

        if(client_slot_ready(slot, readfds)) {
            uint8_t large_buf[2048];
            size_t read_limit = sizeof(large_buf);
            size_t chunk_limit = chunk_limit_fn ? chunk_limit_fn(ctx)
                                                : DATA_TX_MAX_CHUNK_SIZE;
            ssize_t n;

            if (capacity_bytes_fn) {
                size_t capacity = capacity_bytes_fn(ctx);

                if (capacity == 0)
                    continue; /* no queue capacity: leave bytes in the kernel */
                if (capacity < read_limit)
                    read_limit = capacity;
            }

            do {
                n = read(slot->fd, large_buf, read_limit);
            } while(n < 0 && errno == EINTR);

            if(n < 0) {
                if(errno == EAGAIN || errno == EWOULDBLOCK)
                    continue;

                data_tx_log_message(&log, "read error, closing client");
                client_slot_close(slot);
                continue;
            }

            if(n == 0) {
                data_tx_log_message(&log, "EOF, closing client");
                client_slot_close(slot);
                continue;
            }

            data_tx_log_bytes(&log, n);

            size_t processed = data_tx_for_each_chunk(large_buf, (size_t)n,
                                                     chunk_limit, handler, ctx);
            if (processed < (size_t)n)
                data_tx_log_processed(&log, processed, n);
        }
    }
}

