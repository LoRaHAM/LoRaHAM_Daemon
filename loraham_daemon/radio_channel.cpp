#include "radio_channel.h"

#include <RadioLib.h>

#include "daemon_protocol.h"
#include "unix_socket.h"
#include "client_set.h"
#include "client_slot.h"
#include "event_loop.h"

#include <stdio.h>

/* --- Channel socket state ----------------------------------------------- */

void radio_channel_io_init(RadioChannelIo *ch,
                           RadioBand_t band,
                           const char *data_socket_path,
                           const char *conf_socket_path,
                           int *data_listen_fd,
                           int *conf_listen_fd,
                           ClientSlot *data_slots,
                           ClientSlot *conf_slots)
{
    ch->band = band;
    ch->data_socket_path = data_socket_path;
    ch->conf_socket_path = conf_socket_path;
    ch->data_listen_fd = data_listen_fd;
    ch->conf_listen_fd = conf_listen_fd;
    ch->data_slots = data_slots;
    ch->conf_slots = conf_slots;
}



void radio_channel_add_fds(RadioChannelIo *ch, EventLoopSet *set)
{
    event_loop_add_fd(set, *ch->data_listen_fd);
    event_loop_add_fd(set, *ch->conf_listen_fd);

    client_slot_add_to_event_loop_with_output(ch->data_slots,
                                                  MAX_CLIENTS,
                                                  set);
    client_slot_add_to_event_loop_with_output(ch->conf_slots,
                                                  MAX_CLIENTS,
                                                  set);
}

void radio_channel_accept_ready(RadioChannelIo *ch, const EventLoopReadySet *ready)
{
    if(event_loop_ready_fd_read(ready, *ch->data_listen_fd))
        client_slot_accept_with_output(*ch->data_listen_fd,
                                       ch->data_slots,
                                       MAX_CLIENTS);

    if(event_loop_ready_fd_read(ready, *ch->conf_listen_fd))
        client_slot_accept_with_output(*ch->conf_listen_fd,
                                       ch->conf_slots,
                                       MAX_CLIENTS);
}

void radio_channel_open_sockets(RadioChannelIo *ch)
{
    *ch->data_listen_fd = setup_unix_socket(ch->data_socket_path, MAX_CLIENTS);
    *ch->conf_listen_fd = setup_unix_socket(ch->conf_socket_path, MAX_CLIENTS);
}

void radio_channel_flush_ready(RadioChannelIo *ch, const EventLoopReadySet *ready)
{
    client_slot_flush_ready_outputs(ch->data_slots,
                                    MAX_CLIENTS,
                                    ready);
    client_slot_flush_ready_outputs(ch->conf_slots,
                                    MAX_CLIENTS,
                                    ready);
}


/* --- Channel runtime state ---------------------------------------------- */

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
    if(!client_slot_has_clients(io->conf_slots, MAX_CLIENTS) && *rt->getrssi_active) {
        *rt->getrssi_active = false;
        printf("[%s] kein Client mehr verbunden -> GETRSSI auto-stop\n", tag);
        fflush(stdout);
    }
}

/* --- Channel RSSI -------------------------------------------------------- */
// Live-RSSI direkt aus dem SX127x-Register lesen.

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

