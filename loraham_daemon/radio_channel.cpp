#include "radio_channel.h"

#include "daemon_protocol.h"
#include "unix_socket.h"
#include "client_slot.h"
#include "event_loop.h"


/* --- Channel socket state ----------------------------------------------- */

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
                           ClientSlot *conf_slots)
{
    ch->band = band;
    ch->data_socket_path = data_socket_path;
    ch->framed_data_socket_path = framed_data_socket_path;
    ch->conf_socket_path = conf_socket_path;
    ch->data_listen_fd = data_listen_fd;
    ch->framed_data_listen_fd = framed_data_listen_fd;
    ch->conf_listen_fd = conf_listen_fd;
    ch->data_slots = data_slots;
    ch->framed_data_slots = framed_data_slots;
    ch->conf_slots = conf_slots;
}


static void radio_channel_reconcile_slots(ClientSlot *slots,
                                                EventLoopSet *set)
{
    if (!slots)
        return;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        uint32_t events;

        if (!client_slot_has_client(&slots[i]))
            continue;

        events = EVENT_LOOP_EVENT_READ;
        if (client_output_queue_pending(&slots[i].output) > 0)
            events |= EVENT_LOOP_EVENT_WRITE;

        event_loop_reconcile_fd(set,
                                &slots[i],
                                client_slot_generation(&slots[i]),
                                client_slot_fd(&slots[i]),
                                events);
    }
}

void radio_channel_reconcile_fds(RadioChannelIo *ch, EventLoopSet *set)
{
    if (!ch)
        return;

    if (ch->data_listen_fd && *ch->data_listen_fd >= 0) {
        event_loop_reconcile_fd(set,
                                ch->data_listen_fd,
                                1u,
                                *ch->data_listen_fd,
                                EVENT_LOOP_EVENT_READ);
    }

    if (ch->framed_data_listen_fd && *ch->framed_data_listen_fd >= 0) {
        event_loop_reconcile_fd(set,
                                ch->framed_data_listen_fd,
                                1u,
                                *ch->framed_data_listen_fd,
                                EVENT_LOOP_EVENT_READ);
    }

    if (ch->conf_listen_fd && *ch->conf_listen_fd >= 0) {
        event_loop_reconcile_fd(set,
                                ch->conf_listen_fd,
                                1u,
                                *ch->conf_listen_fd,
                                EVENT_LOOP_EVENT_READ);
    }

    radio_channel_reconcile_slots(ch->data_slots, set);
    radio_channel_reconcile_slots(ch->framed_data_slots, set);
    radio_channel_reconcile_slots(ch->conf_slots, set);
}

void radio_channel_accept_ready(RadioChannelIo *ch, const EventLoopReadySet *ready)
{
    if(event_loop_ready_fd_read(ready, *ch->data_listen_fd))
        client_slot_accept_with_output(*ch->data_listen_fd,
                                       ch->data_slots,
                                       MAX_CLIENTS);

    if(event_loop_ready_fd_read(ready, *ch->framed_data_listen_fd))
        client_slot_accept_with_output(*ch->framed_data_listen_fd,
                                       ch->framed_data_slots,
                                       MAX_CLIENTS);

    if(event_loop_ready_fd_read(ready, *ch->conf_listen_fd))
        client_slot_accept_with_output(*ch->conf_listen_fd,
                                       ch->conf_slots,
                                       MAX_CLIENTS);
}

int radio_channel_open_sockets(RadioChannelIo *ch)
{
    *ch->data_listen_fd = setup_unix_socket(ch->data_socket_path, MAX_CLIENTS);
    if (*ch->data_listen_fd < 0)
        return -1;

    *ch->framed_data_listen_fd = setup_unix_socket(ch->framed_data_socket_path,
                                                   MAX_CLIENTS);
    if (*ch->framed_data_listen_fd < 0) {
        close_unix_socket(ch->data_listen_fd, ch->data_socket_path);
        return -1;
    }

    *ch->conf_listen_fd = setup_unix_socket(ch->conf_socket_path, MAX_CLIENTS);
    if (*ch->conf_listen_fd < 0) {
        close_unix_socket(ch->framed_data_listen_fd,
                          ch->framed_data_socket_path);
        close_unix_socket(ch->data_listen_fd, ch->data_socket_path);
        return -1;
    }

    return 0;
}

void radio_channel_flush_ready(RadioChannelIo *ch, const EventLoopReadySet *ready)
{
    client_slot_flush_ready_outputs(ch->data_slots,
                                    MAX_CLIENTS,
                                    ready);
    client_slot_flush_ready_outputs(ch->framed_data_slots,
                                    MAX_CLIENTS,
                                    ready);
    client_slot_flush_ready_outputs(ch->conf_slots,
                                    MAX_CLIENTS,
                                    ready);
}

/* --- Channel RSSI -------------------------------------------------------- */
// Live RSSI now lives in the chip-specific driver
// (RadioDriver::readLiveRssi); only socket and client logic remains here.

