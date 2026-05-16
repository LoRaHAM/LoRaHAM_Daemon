#include "radio_channel.h"

#include <RadioLib.h>

#include "daemon_protocol.h"
#include "unix_socket.h"
#include "client_set.h"
#include "event_loop.h"

#include <stdio.h>

/* --- Radio channel socket/client state --- */

void radio_channel_io_init(RadioChannelIo *ch,
                           RadioBand_t band,
                           const char *data_socket_path,
                           const char *conf_socket_path,
                           int *data_listen_fd,
                           int *conf_listen_fd,
                           int *data_clients,
                           int *conf_clients)
{
    ch->band = band;
    ch->data_socket_path = data_socket_path;
    ch->conf_socket_path = conf_socket_path;
    ch->data_listen_fd = data_listen_fd;
    ch->conf_listen_fd = conf_listen_fd;
    ch->data_clients = data_clients;
    ch->conf_clients = conf_clients;
}


void radio_channel_accept_ready(RadioChannelIo *ch, const fd_set *ready)
{
    if(event_loop_select_ready_fd(ready, *ch->data_listen_fd))
        client_set_accept(*ch->data_listen_fd, ch->data_clients, MAX_CLIENTS);

    if(event_loop_select_ready_fd(ready, *ch->conf_listen_fd))
        client_set_accept(*ch->conf_listen_fd, ch->conf_clients, MAX_CLIENTS);
}

void radio_channel_open_sockets(RadioChannelIo *ch)
{
    *ch->data_listen_fd = setup_unix_socket(ch->data_socket_path, MAX_CLIENTS);
    *ch->conf_listen_fd = setup_unix_socket(ch->conf_socket_path, MAX_CLIENTS);
}


/* --- Radio channel runtime flags --- */

void radio_channel_runtime_init(RadioChannelRuntime *rt,
                                volatile RadioMode_t *mode,
                                volatile bool *received_flag,
                                volatile bool *tx_busy,
                                volatile bool *cad_active,
                                volatile bool *getrssi_active)
{
    rt->mode = mode;
    rt->received_flag = received_flag;
    rt->tx_busy = tx_busy;
    rt->cad_active = cad_active;
    rt->getrssi_active = getrssi_active;
}

void radio_channel_getrssi_autostop(RadioChannelIo *io,
                                    RadioChannelRuntime *rt,
                                    const char *tag)
{
    if(!client_set_has_clients(io->conf_clients, MAX_CLIENTS) && *rt->getrssi_active) {
        *rt->getrssi_active = false;
        printf("[%s] kein Client mehr verbunden -> GETRSSI auto-stop\n", tag);
        fflush(stdout);
    }
}

/* --- Radio channel RSSI --- */

float radio_channel_read_live_rssi(Module *mod,
                                   volatile RadioMode_t mode,
                                   bool is_hf)
{
    uint8_t reg = (mode == RADIO_MODE_LORA) ? 0x1B : 0x11;
    int16_t raw = mod->SPIgetRegValue(reg, 7, 0);

    if (raw < 0)
        return -200.0f;

    if (mode == RADIO_MODE_LORA)
        return (is_hf ? -157.0f : -164.0f) + (float)raw;

    return -((float)raw) / 2.0f;
}

