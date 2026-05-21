#ifndef LHKT_TCP_SERVER_H
#define LHKT_TCP_SERVER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int lhkt_tcp_server_listen(const char *host, int port);
int lhkt_tcp_server_accept(int listen_fd, char *peer, size_t peer_size);
void lhkt_tcp_server_close(int fd);

#ifdef __cplusplus
}
#endif

#endif
