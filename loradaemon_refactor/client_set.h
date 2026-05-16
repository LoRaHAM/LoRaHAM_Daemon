#ifndef LORAHAM_CLIENT_SET_H
#define LORAHAM_CLIENT_SET_H

#include <stddef.h>
#include <stdint.h>
#include <sys/select.h>
#include <sys/types.h>

/* --- Client slot handling --- */

int client_set_add(int *clients, int max_clients, int fd);
int client_set_accept(int listen_fd, int *clients, int max_clients);
void client_set_close_slot(int *clients, int index);
ssize_t client_set_read_slot(int *clients, int index, void *buf, size_t len);
void client_set_add_fds(int *clients, int max_clients, fd_set *readfds, int *maxfd);
int client_set_slot_ready(int *clients, int index, const fd_set *ready);
int client_set_has_clients(int *clients, int max_clients);

/* --- Client text broadcast --- */

void client_set_broadcast(int *clients, int max_clients, const char *msg);
void client_set_broadcast_bytes(int *clients, int max_clients, const uint8_t *buf, size_t len);

#endif
