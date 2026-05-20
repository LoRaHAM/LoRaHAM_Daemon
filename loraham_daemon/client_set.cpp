#include "client_set.h"

#include "event_loop.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

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


static int client_set_add_with_output(int *clients, ClientOutputQueue *queues, int max_clients, int fd)
{
    for (int i = 0; i < max_clients; i++) {
        if (clients[i] == 0) {
            clients[i] = fd;
            if (queues)
                client_output_queue_reset(&queues[i]);
            return 1;
        }
    }

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


int client_set_accept_with_output(int listen_fd, int *clients, ClientOutputQueue *queues, int max_clients)
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

    if (!client_set_add_with_output(clients, queues, max_clients, fd)) {
        close(fd);
        errno = EMFILE;
        return -1;
    }

    return fd;
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

void client_set_close_slot_with_output(int *clients, ClientOutputQueue *queues, int index)
{
    client_set_close_slot(clients, index);

    if (queues)
        client_output_queue_reset(&queues[index]);
}

void client_set_close_all(int *clients, int max_clients)
{
    for (int i = 0; i < max_clients; i++)
        client_set_close_slot(clients, i);
}

void client_set_close_all_with_output(int *clients, ClientOutputQueue *queues, int max_clients)
{
    for (int i = 0; i < max_clients; i++)
        client_set_close_slot_with_output(clients, queues, i);
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

ssize_t client_set_read_slot_with_output(int *clients,
                                        ClientOutputQueue *queues,
                                        int index,
                                        void *buf,
                                        size_t len)
{
    ssize_t n = client_set_read_slot(clients, index, buf, len);

    if (clients[index] <= 0 && queues)
        client_output_queue_reset(&queues[index]);

    return n;
}

int client_set_slot_ready(int *clients, int index, const EventLoopReadySet *ready)
{
    return clients[index] > 0 && event_loop_ready_fd_read(ready, clients[index]);
}

int client_set_output_ready(int *clients,
                            ClientOutputQueue *queues,
                            int index,
                            const EventLoopReadySet *ready)
{
    return clients[index] > 0 && queues &&
           client_output_queue_pending(&queues[index]) > 0 &&
           event_loop_ready_fd_write(ready, clients[index]);
}

void client_set_add_to_event_loop(int *clients, int max_clients, EventLoopSet *set)
{
    for (int i = 0; i < max_clients; i++) {
        if (clients[i] > 0)
            event_loop_add_fd(set, clients[i]);
    }
}

void client_set_add_to_event_loop_with_output(int *clients,
                                              ClientOutputQueue *queues,
                                              int max_clients,
                                              EventLoopSet *set)
{
    for (int i = 0; i < max_clients; i++) {
        if (clients[i] > 0) {
            uint32_t events = EVENT_LOOP_EVENT_READ;

            if (queues && client_output_queue_pending(&queues[i]) > 0)
                events |= EVENT_LOOP_EVENT_WRITE;

            event_loop_add_fd_events(set, clients[i], events);
        }
    }
}

/* --- Client queued writes ------------------------------------------------ */
void client_set_flush_output_slot(int *clients, ClientOutputQueue *queues, int index)
{
    if (!queues)
        return;

    if (clients[index] <= 0) {
        client_output_queue_reset(&queues[index]);
        return;
    }

    if (client_output_queue_flush_fd(clients[index], &queues[index]) < 0)
        client_set_close_slot_with_output(clients, queues, index);
}

void client_set_flush_outputs(int *clients, ClientOutputQueue *queues, int max_clients)
{
    if (!queues)
        return;

    for (int i = 0; i < max_clients; i++)
        client_set_flush_output_slot(clients, queues, i);
}

void client_set_flush_ready_outputs(int *clients,
                                    ClientOutputQueue *queues,
                                    int max_clients,
                                    const EventLoopReadySet *ready)
{
    if (!queues)
        return;

    for (int i = 0; i < max_clients; i++) {
        if (client_set_output_ready(clients, queues, i, ready))
            client_set_flush_output_slot(clients, queues, i);
    }
}

/* --- Client queued broadcasts ------------------------------------------- */
void client_set_broadcast_bytes_queued(int *clients, ClientOutputQueue *queues, int max_clients, const uint8_t *buf, size_t len)
{
    if (len == 0)
        return;

    if (!buf || !queues)
        return;

    for (int i = 0; i < max_clients; i++) {
        if (clients[i] <= 0) {
            client_output_queue_reset(&queues[i]);
            continue;
        }

        if (!client_output_queue_append(&queues[i], buf, len)) {
            client_set_close_slot_with_output(clients, queues, i);
            continue;
        }

        client_set_flush_output_slot(clients, queues, i);
    }
}

void client_set_broadcast_queued(int *clients, ClientOutputQueue *queues, int max_clients, const char *msg)
{
    if (!msg)
        return;

    client_set_broadcast_bytes_queued(clients, queues, max_clients,
                                      (const uint8_t *)msg, strlen(msg));
}
