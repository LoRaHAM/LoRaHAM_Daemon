#include "tcp_server.h"
#include "loraham_kiss_tnc.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/*
 * Small IPv4 TCP server for one KISS/TCP client.
 * Multi-client handling is intentionally out of scope.
 */

int lhkt_tcp_server_listen(const char *host, int port)
{
    int fd;
    int yes;
    struct sockaddr_in addr;

    if (!host || host[0] == '\0' || port < 1 || port > 65535) {
        return LHKT_ERR_FORMAT;
    }

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return LHKT_ERR;
    }

    yes = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        close(fd);
        return LHKT_ERR;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        close(fd);
        return LHKT_ERR_FORMAT;
    }

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return LHKT_ERR;
    }

    if (listen(fd, 1) < 0) {
        close(fd);
        return LHKT_ERR;
    }

    return fd;
}

int lhkt_tcp_server_accept(int listen_fd, char *peer, size_t peer_size)
{
    int fd;
    struct sockaddr_in addr;
    socklen_t addr_len;
    char ip[INET_ADDRSTRLEN];

    if (listen_fd < 0) {
        return LHKT_ERR;
    }

    addr_len = sizeof(addr);

    for (;;) {
        fd = accept(listen_fd, (struct sockaddr *)&addr, &addr_len);
        if (fd >= 0) {
            break;
        }

        if (errno == EINTR) {
            continue;
        }

        return LHKT_ERR;
    }

    if (peer && peer_size > 0) {
        if (inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip))) {
            snprintf(peer, peer_size, "%s:%u", ip, (unsigned int)ntohs(addr.sin_port));
        } else {
            snprintf(peer, peer_size, "unknown");
        }
    }

    return fd;
}

void lhkt_tcp_server_close(int fd)
{
    if (fd >= 0) {
        close(fd);
    }
}
