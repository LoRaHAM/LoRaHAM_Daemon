#include "unix_socket.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/* --- Unix socket lifecycle ---------------------------------------------- */
// Unix-Socket anlegen wie im ursprünglichen In-File-Helper.
int setup_unix_socket(const char *path, int backlog)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    unlink(path);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (listen(fd, backlog) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    return fd;
}

// Unix-Socket schließen und Socket-Datei entfernen.
void close_unix_socket(int *fd, const char *path)
{
    if (fd && *fd >= 0) {
        close(*fd);
        *fd = -1;
    }

    if (path)
        unlink(path);
}
