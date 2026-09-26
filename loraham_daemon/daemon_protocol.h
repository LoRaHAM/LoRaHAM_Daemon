#ifndef LORAHAM_DAEMON_PROTOCOL_H
#define LORAHAM_DAEMON_PROTOCOL_H

/* --- Public daemon limits --- */

#define buf_SIZE 256
#define MAX_CLIENTS 10

/* --- CAD monitoring cadence --- */

#define DAEMON_CAD_POLL_INTERVAL_MS 200

/* --- Public Unix socket paths --- */
/*
 * Production sockets live in the protected shared runtime directory
 * /run/loraham (loraham:loraham, mode 2750, provisioned via tmpfiles.d):
 * group members connect, and only the unprivileged daemon user `loraham`
 * (the directory owner) can create files there — the old /tmp namespace
 * allowed any local user to squat or impersonate the paths.
 * LORAHAM_SOCKET_DIR overrides the directory for non-root dev/test runs
 * ONLY (resolved once in daemon_band_resolve); the systemd unit clears it.
 * The separate lock namespace /run/lock/loraham is unaffected.
 */

#define LORAHAM_SOCKET_DIR_DEFAULT "/run/loraham"

#define DATA868_SOCKET_NAME "lora868.sock"
#define DATA433_SOCKET_NAME "lora433.sock"
#define DATA868_FRAMED_SOCKET_NAME "lora868f.sock"
#define DATA433_FRAMED_SOCKET_NAME "lora433f.sock"
#define CONF868_SOCKET_NAME "loraconf868.sock"
#define CONF433_SOCKET_NAME "loraconf433.sock"

#define DATA868_SOCKET LORAHAM_SOCKET_DIR_DEFAULT "/" DATA868_SOCKET_NAME
#define DATA433_SOCKET LORAHAM_SOCKET_DIR_DEFAULT "/" DATA433_SOCKET_NAME
#define DATA868_FRAMED_SOCKET LORAHAM_SOCKET_DIR_DEFAULT "/" DATA868_FRAMED_SOCKET_NAME
#define DATA433_FRAMED_SOCKET LORAHAM_SOCKET_DIR_DEFAULT "/" DATA433_FRAMED_SOCKET_NAME
#define CONF868_SOCKET LORAHAM_SOCKET_DIR_DEFAULT "/" CONF868_SOCKET_NAME
#define CONF433_SOCKET LORAHAM_SOCKET_DIR_DEFAULT "/" CONF433_SOCKET_NAME

/* --- Test aliases --- */

#define SOCK_DATA_433 DATA433_SOCKET
#define SOCK_DATA_868 DATA868_SOCKET
#define SOCK_DATA_FRAMED_433 DATA433_FRAMED_SOCKET
#define SOCK_DATA_FRAMED_868 DATA868_FRAMED_SOCKET
#define SOCK_CONF_433 CONF433_SOCKET
#define SOCK_CONF_868 CONF868_SOCKET

#endif
