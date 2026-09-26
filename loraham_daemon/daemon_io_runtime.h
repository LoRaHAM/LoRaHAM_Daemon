#ifndef LORAHAM_DAEMON_IO_RUNTIME_H
#define LORAHAM_DAEMON_IO_RUNTIME_H

#include "client_slot.h"
#include "daemon_protocol.h"
#include "event_loop.h"
#include "framed_data_tx.h"
#include "radio_channel.h"

/* --- Daemon I/O runtime state ------------------------------------------- */
/* One band per process: one socket trio, one slot set, one channel. */

extern int data_fd;
extern int data_framed_fd;
extern int conf_fd;

extern ClientSlot client_data_slots[MAX_CLIENTS];
extern ClientSlot client_data_framed_slots[MAX_CLIENTS];
extern FramedDataTxState client_framed_states[MAX_CLIENTS];
extern ClientSlot client_conf_slots[MAX_CLIENTS];

extern RadioChannelIo channel;

/* --- Daemon I/O lifecycle ----------------------------------------------- */

void daemon_io_startup_cleanup(void);
void daemon_io_shutdown_cleanup(void);
void daemon_io_init(void);
void daemon_io_sync_event_fds(EventLoopSet *event_set);

#endif
