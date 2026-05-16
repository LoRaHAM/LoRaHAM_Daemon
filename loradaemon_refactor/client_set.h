#ifndef LORAHAM_CLIENT_SET_H
#define LORAHAM_CLIENT_SET_H

#include <sys/select.h>

/* --- Client slot handling --- */

int client_set_add(int *clients, int max_clients, int fd);
void client_set_close_slot(int *clients, int index);
void client_set_add_fds(int *clients, int max_clients, fd_set *readfds, int *maxfd);

/* --- Client text broadcast --- */

void client_set_broadcast(int *clients, int max_clients, const char *msg);

#endif
