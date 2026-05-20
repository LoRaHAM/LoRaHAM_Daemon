#ifndef LORAHAM_DAEMON_PROTOCOL_H
#define LORAHAM_DAEMON_PROTOCOL_H

/* --- Public daemon limits --- */

#define buf_SIZE 256
#define MAX_CLIENTS 10

/* --- CAD loop timing --- */

#define CAD_POLL_INTERVAL 30

/* --- Public Unix socket paths --- */

#define DATA868_SOCKET "/tmp/lora868.sock"
#define DATA433_SOCKET "/tmp/lora433.sock"
#define CONF868_SOCKET "/tmp/loraconf868.sock"
#define CONF433_SOCKET "/tmp/loraconf433.sock"

/* --- Test aliases --- */

#define SOCK_DATA_433 DATA433_SOCKET
#define SOCK_DATA_868 DATA868_SOCKET
#define SOCK_CONF_433 CONF433_SOCKET
#define SOCK_CONF_868 CONF868_SOCKET

#endif
