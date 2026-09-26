#ifndef LORAHAM_RADIO_CHANNEL_H
#define LORAHAM_RADIO_CHANNEL_H

#include <stdbool.h>
#include <sys/select.h>
#include "event_loop.h"
#include "client_slot.h"

/* --- Radio channel identity --- */

typedef enum {
    RADIO_BAND_433 = 433,
    RADIO_BAND_868 = 868
} RadioBand_t;

/* --- Radio modem mode --- */

typedef enum {
    RADIO_MODE_LORA,
    RADIO_MODE_FSK
} RadioMode_t;

/* --- Radio channel socket/client state --- */

typedef struct {
    RadioBand_t band;
    const char *data_socket_path;
    const char *framed_data_socket_path;
    const char *conf_socket_path;
    int *data_listen_fd;
    int *framed_data_listen_fd;
    int *conf_listen_fd;
    ClientSlot *data_slots;
    ClientSlot *framed_data_slots;
    ClientSlot *conf_slots;
} RadioChannelIo;

void radio_channel_io_init(RadioChannelIo *ch,
                           RadioBand_t band,
                           const char *data_socket_path,
                           const char *framed_data_socket_path,
                           const char *conf_socket_path,
                           int *data_listen_fd,
                           int *framed_data_listen_fd,
                           int *conf_listen_fd,
                           ClientSlot *data_slots,
                           ClientSlot *framed_data_slots,
                           ClientSlot *conf_slots);

int radio_channel_open_sockets(RadioChannelIo *ch);
void radio_channel_reconcile_fds(RadioChannelIo *ch, EventLoopSet *set);
void radio_channel_accept_ready(RadioChannelIo *ch, const EventLoopReadySet *ready);
void radio_channel_flush_ready(RadioChannelIo *ch, const EventLoopReadySet *ready);

/* Live-RSSI reads moved into the RadioDriver (RadioDriver::readLiveRssi):
 * the raw register access is chip-specific and lives in the driver. */

#endif
