#include "client_set.h"

#include "event_loop.h"

#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* --- Client slot handling --- */

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

    if (fd < 0)
        return fd;

    client_set_add(clients, max_clients, fd);
    return fd;
}

void client_set_close_slot(int *clients, int index)
{
    if (clients[index] > 0)
        close(clients[index]);

    clients[index] = 0;
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
    ssize_t n = read(clients[index], buf, len);

    if (n <= 0)
        client_set_close_slot(clients, index);

    return n;
}

int client_set_slot_ready(int *clients, int index, const fd_set *ready)
{
    return clients[index] > 0 && event_loop_select_ready_fd(ready, clients[index]);
}

void client_set_add_fds(int *clients, int max_clients, fd_set *readfds, int *maxfd)
{
    for (int i = 0; i < max_clients; i++) {
        if (clients[i] > 0)
            FD_SET(clients[i], readfds);

        if (clients[i] >= *maxfd)
            *maxfd = clients[i] + 1;
    }
}

/* --- Client text broadcast --- */

void client_set_broadcast(int *clients, int max_clients, const char *msg)
{
    size_t len = strlen(msg);

    for (int i = 0; i < max_clients; i++) {
        if (clients[i] > 0)
            write(clients[i], msg, len);
    }
}


void client_set_broadcast_bytes(int *clients, int max_clients, const uint8_t *buf, size_t len)
{
    for (int i = 0; i < max_clients; i++) {
        if (clients[i] > 0)
            write(clients[i], buf, len);
    }
}
