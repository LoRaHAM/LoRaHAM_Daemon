#ifndef LORAHAM_UNIX_SOCKET_H
#define LORAHAM_UNIX_SOCKET_H

/* Unix socket setup for local daemon IPC. */
int setup_unix_socket(const char *path, int backlog);
void close_unix_socket(int *fd, const char *path);

#endif
