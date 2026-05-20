#include "client_output_queue.h"

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/* --- Client output queue ------------------------------------------------- */

void client_output_queue_init(ClientOutputQueue *queue)
{
    if (queue)
        queue->len = 0;
}

void client_output_queue_init_all(ClientOutputQueue *queues, int count)
{
    if (!queues || count <= 0)
        return;

    for (int i = 0; i < count; i++)
        client_output_queue_init(&queues[i]);
}

void client_output_queue_reset(ClientOutputQueue *queue)
{
    client_output_queue_init(queue);
}

size_t client_output_queue_pending(const ClientOutputQueue *queue)
{
    return queue ? queue->len : 0;
}

const uint8_t *client_output_queue_data(const ClientOutputQueue *queue)
{
    if (!queue || queue->len == 0)
        return NULL;

    return queue->data;
}

int client_output_queue_append(ClientOutputQueue *queue, const uint8_t *buf, size_t len)
{
    if (len == 0)
        return 1;

    if (!queue || !buf) {
        errno = EINVAL;
        return 0;
    }

    if (len > CLIENT_OUTPUT_QUEUE_CAPACITY - queue->len) {
        errno = ENOBUFS;
        return 0;
    }

    memcpy(queue->data + queue->len, buf, len);
    queue->len += len;

    return 1;
}

size_t client_output_queue_consume(ClientOutputQueue *queue, size_t len)
{
    size_t used;

    if (!queue || len == 0)
        return 0;

    used = len < queue->len ? len : queue->len;

    if (used == queue->len) {
        queue->len = 0;
    } else if (used > 0) {
        memmove(queue->data, queue->data + used, queue->len - used);
        queue->len -= used;
    }

    return used;
}

ssize_t client_output_queue_flush_fd(int fd, ClientOutputQueue *queue)
{
    ssize_t total = 0;

    if (!queue) {
        errno = EINVAL;
        return -1;
    }

    while (queue->len > 0) {
        ssize_t n = send(fd, queue->data, queue->len, MSG_NOSIGNAL);

        if (n > 0) {
            client_output_queue_consume(queue, (size_t)n);
            total += n;
            continue;
        }

        if (n < 0 && errno == EINTR)
            continue;

        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return total;

        if (n == 0)
            errno = EPIPE;

        return -1;
    }

    return total;
}
