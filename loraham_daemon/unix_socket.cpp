#include "unix_socket.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

/* --- Unix socket lifecycle ---------------------------------------------- */

static int unix_socket_path_is_socket(const char *path)
{
    struct stat st;

    if (lstat(path, &st) != 0) {
        if (errno == ENOENT)
            return 0;

        return -1;
    }

    return S_ISSOCK(st.st_mode) ? 1 : -1;
}

static int unix_socket_prepare_path(const char *path)
{
    int state = unix_socket_path_is_socket(path);

    if (state == 0)
        return 0;

    if (state < 0) {
        fprintf(stderr,
                "ERROR: socket path exists but is not a socket: %s\n",
                path ? path : "");
        errno = EEXIST;
        return -1;
    }

    if (unlink(path) != 0)
        return -1;

    return 0;
}

// Unix-Socket anlegen wie im ursprünglichen In-File-Helper.
int setup_unix_socket(const char *path, int backlog)
{
    int fd;
    struct sockaddr_un addr;

    if (!path || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    if (strlen(path) >= sizeof(addr.sun_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    if (unix_socket_prepare_path(path) != 0)
        return -1;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    if (listen(fd, backlog) < 0) {
        close(fd);
        unlink(path);
        return -1;
    }

    return fd;
}

static void unix_socket_unlink_socket_path(const char *path)
{
    if (!path)
        return;

    if (unix_socket_path_is_socket(path) == 1)
        unlink(path);
}

// Unix-Socket schließen und Socket-Datei entfernen.
void close_unix_socket(int *fd, const char *path)
{
    if (fd && *fd >= 0) {
        close(*fd);
        *fd = -1;
    }

    unix_socket_unlink_socket_path(path);
}
