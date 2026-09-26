#ifndef LORAHAM_DAEMON_SOCKET_RUNTIME_H
#define LORAHAM_DAEMON_SOCKET_RUNTIME_H

#include "event_loop.h"
#include "radio_channel.h"

/* --- Socket runtime helpers --------------------------------------------- */

void daemon_accept_channel_logged(RadioChannelIo *channel,
                                  const EventLoopReadySet *readfds,
                                  const char *ctx);

void daemon_flush_channel_logged(RadioChannelIo *channel,
                                 const EventLoopReadySet *readfds,
                                 const char *ctx);

#endif
