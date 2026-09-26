#include "daemon_socket_runtime.h"

#include "client_slot.h"
#include "daemon_log.h"
#include "daemon_protocol.h"

/* --- Socket runtime helpers --------------------------------------------- */

static int daemon_client_slot_count(ClientSlot *slots, int max_clients)
{
    int count = 0;

    if (!slots)
        return 0;

    for (int i = 0; i < max_clients; i++) {
        if (client_slot_has_client(&slots[i]))
            count++;
    }

    return count;
}

static bool daemon_client_slots_output_ready(ClientSlot *slots,
                                             int max_clients,
                                             const EventLoopReadySet *readfds)
{
    if (!slots)
        return false;

    for (int i = 0; i < max_clients; i++) {
        if (client_slot_output_ready(&slots[i], readfds))
            return true;
    }

    return false;
}

static void daemon_log_accept_delta(const char *ctx,
                                    const char *kind,
                                    int before,
                                    int after)
{
    if (after > before)
        daemon_debug_ctx(ctx, "%s client connected (%d)", kind, after);
    else
        daemon_debug_ctx(ctx, "%s accept without a new client", kind);
}

void daemon_accept_channel_logged(RadioChannelIo *channel,
                                         const EventLoopReadySet *readfds,
                                         const char *ctx)
{
    bool data_ready = event_loop_ready_fd_read(readfds, *channel->data_listen_fd);
    bool framed_ready = event_loop_ready_fd_read(readfds,
                                                 *channel->framed_data_listen_fd);
    bool conf_ready = event_loop_ready_fd_read(readfds, *channel->conf_listen_fd);
    int data_before = daemon_client_slot_count(channel->data_slots, MAX_CLIENTS);
    int framed_before = daemon_client_slot_count(channel->framed_data_slots,
                                                 MAX_CLIENTS);
    int conf_before = daemon_client_slot_count(channel->conf_slots, MAX_CLIENTS);

    if (data_ready)
        daemon_debug_ctx(ctx, "DATA accept ready");
    if (framed_ready)
        daemon_debug_ctx(ctx, "DATAF accept ready");
    if (conf_ready)
        daemon_debug_ctx(ctx, "CONF accept ready");

    radio_channel_accept_ready(channel, readfds);

    if (data_ready) {
        int data_after = daemon_client_slot_count(channel->data_slots, MAX_CLIENTS);
        daemon_log_accept_delta(ctx, "DATA", data_before, data_after);
    }

    if (framed_ready) {
        int framed_after = daemon_client_slot_count(channel->framed_data_slots,
                                                    MAX_CLIENTS);
        daemon_log_accept_delta(ctx, "DATAF", framed_before, framed_after);
    }

    if (conf_ready) {
        int conf_after = daemon_client_slot_count(channel->conf_slots, MAX_CLIENTS);
        daemon_log_accept_delta(ctx, "CONF", conf_before, conf_after);
    }
}

void daemon_flush_channel_logged(RadioChannelIo *channel,
                                        const EventLoopReadySet *readfds,
                                        const char *ctx)
{
    if (daemon_client_slots_output_ready(channel->data_slots, MAX_CLIENTS, readfds))
        daemon_debug_ctx(ctx, "DATA output ready");

    if (daemon_client_slots_output_ready(channel->framed_data_slots,
                                         MAX_CLIENTS, readfds))
        daemon_debug_ctx(ctx, "DATAF output ready");

    if (daemon_client_slots_output_ready(channel->conf_slots, MAX_CLIENTS, readfds))
        daemon_debug_ctx(ctx, "CONF output ready");

    radio_channel_flush_ready(channel, readfds);
}
