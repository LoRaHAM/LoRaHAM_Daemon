#ifndef LORAHAM_RADIO_CHANNEL_H
#define LORAHAM_RADIO_CHANNEL_H

#include <stdbool.h>
#include <sys/select.h>

class Module;

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
    const char *conf_socket_path;
    int *data_listen_fd;
    int *conf_listen_fd;
    int *data_clients;
    int *conf_clients;
} RadioChannelIo;

void radio_channel_io_init(RadioChannelIo *ch,
                           RadioBand_t band,
                           const char *data_socket_path,
                           const char *conf_socket_path,
                           int *data_listen_fd,
                           int *conf_listen_fd,
                           int *data_clients,
                           int *conf_clients);

void radio_channel_open_sockets(RadioChannelIo *ch);
void radio_channel_accept_ready(RadioChannelIo *ch, const fd_set *ready);


/* --- Radio channel RSSI --- */

float radio_channel_read_live_rssi(Module *mod,
                                   volatile RadioMode_t mode,
                                   bool is_hf);

/* --- Radio channel runtime flags --- */

typedef struct {
    volatile RadioMode_t *mode;
    volatile bool *received_flag;
    volatile bool *tx_busy;
    volatile bool *cad_active;
    volatile bool *getrssi_active;
} RadioChannelRuntime;

void radio_channel_runtime_init(RadioChannelRuntime *rt,
                                volatile RadioMode_t *mode,
                                volatile bool *received_flag,
                                volatile bool *tx_busy,
                                volatile bool *cad_active,
                                volatile bool *getrssi_active);

void radio_channel_getrssi_autostop(RadioChannelIo *io,
                                    RadioChannelRuntime *rt,
                                    const char *tag);

#endif
