#include "client_set.h"

#include <string.h>
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

void client_set_close_slot(int *clients, int index)
{
    if (clients[index] > 0)
        close(clients[index]);

    clients[index] = 0;
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
