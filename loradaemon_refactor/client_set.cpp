#include "client_set.h"

#include "event_loop.h"

#include <errno.h>
#include <fcntl.h>
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

/* --- Client slots -------------------------------------------------------- */

int client_set_set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags < 0)
        return -1;

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        return -1;

    return 0;
}


int client_set_add(int *clients, int max_clients, int fd)
{
    for (int i = 0; i < max_clients; i++) {
        if (clients[i] == 0) {
            clients[i] = fd;
            return 1;
        }
    }

    return 0;
}


int client_set_accept(int listen_fd, int *clients, int max_clients)
{
    int fd = accept(listen_fd, NULL, NULL);
    int saved_errno;

    if (fd < 0)
        return fd;

    if (client_set_set_nonblocking(fd) != 0) {
        saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }

    if (!client_set_add(clients, max_clients, fd)) {
        close(fd);
        errno = EMFILE;
        return -1;
    }

    return fd;
}

void client_set_close_slot(int *clients, int index)
{
    if (clients[index] > 0)
        close(clients[index]);

    clients[index] = 0;
}

void client_set_close_all(int *clients, int max_clients)
{
    for (int i = 0; i < max_clients; i++)
        client_set_close_slot(clients, i);
}


int client_set_has_clients(int *clients, int max_clients)
{
    for (int i = 0; i < max_clients; i++) {
        if (clients[i] > 0)
            return 1;
    }

    return 0;
}



ssize_t client_set_read_slot(int *clients, int index, void *buf, size_t len)
{
    ssize_t n;

    do {
        n = read(clients[index], buf, len);
    } while (n < 0 && errno == EINTR);

    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return n;

        client_set_close_slot(clients, index);
        return n;
    }

    if (n == 0)
        client_set_close_slot(clients, index);

    return n;
}

int client_set_slot_ready(int *clients, int index, const EventLoopReadySet *ready)
{
    return clients[index] > 0 && event_loop_ready_fd(ready, clients[index]);
}

void client_set_add_to_event_loop(int *clients, int max_clients, EventLoopSet *set)
{
    for (int i = 0; i < max_clients; i++) {
        if (clients[i] > 0)
            event_loop_add_fd(set, clients[i]);
    }
}

/* --- Client writes ------------------------------------------------------- */
static ssize_t client_set_send_all(int fd, const uint8_t *buf, size_t len)
{
    size_t sent = 0;

    if (len == 0)
        return 0;

    if (!buf) {
        errno = EINVAL;
        return -1;
    }

    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);

        if (n < 0) {
            if (errno == EINTR)
                continue;

            return -1;
        }

        if (n == 0) {
            errno = EPIPE;
            return -1;
        }

        sent += (size_t)n;
    }

    return (ssize_t)sent;
}

/* --- Client broadcasts --------------------------------------------------- */
// Statusmeldungen an alle verbundenen Clients senden.

void client_set_broadcast(int *clients, int max_clients, const char *msg)
{
    size_t len;

    if (!msg)
        return;

    len = strlen(msg);

    for (int i = 0; i < max_clients; i++) {
        if (clients[i] > 0 &&
            client_set_send_all(clients[i], (const uint8_t *)msg, len) < 0)
            client_set_close_slot(clients, i);
    }
}


// Rohdaten an alle verbundenen Clients senden.
void client_set_broadcast_bytes(int *clients, int max_clients, const uint8_t *buf, size_t len)
{
    if (len > 0 && !buf)
        return;

    for (int i = 0; i < max_clients; i++) {
        if (clients[i] > 0 &&
            client_set_send_all(clients[i], buf, len) < 0)
            client_set_close_slot(clients, i);
    }
}
