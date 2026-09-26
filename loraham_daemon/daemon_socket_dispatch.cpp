#include "daemon_socket_dispatch.h"

#include "daemon_band.h"
#include "daemon_data_tx_runtime.h"
#include "daemon_framed_data_runtime.h"
#include "daemon_io_runtime.h"
#include "daemon_socket_runtime.h"
#include "data_tx.h"

/* --- Socket dispatch orchestration -------------------------------------- */
/* One band per process: accept -> raw DATA -> framed DATA -> drain
 * completions -> CONF -> flush. */

void daemon_process_ready_sockets(ConfigDispatchContext *config_ctx,
                                  DataTxDaemonContext *data_tx_ctx,
                                  const EventLoopReadySet *readfds,
                                  uint8_t *buf)
{
    const DaemonBandDescriptor *band = daemon_band();

    daemon_accept_channel_logged(&channel, readfds, band->client_log_ctx);
    data_tx_process_slots(band->tag, client_data_slots, MAX_CLIENTS,
                          readfds, send_data_chunk, data_tx_ctx,
                          daemon_data_tx_log(band->tx_log_ctx),
                          data_tx_queue_capacity_bytes,
                          data_tx_chunk_limit_bytes);
    daemon_process_framed_data_slots(band->framed_log_ctx,
                                     client_data_framed_slots,
                                     client_framed_states,
                                     MAX_CLIENTS,
                                     readfds,
                                     send_framed_data_packet,
                                     data_tx_ctx,
                                     daemon_data_tx_result_enabled,
                                     daemon_data_tx_next_result_seq,
                                     daemon_data_tx_result_deferred,
                                     daemon_data_tx_set_completion_target,
                                     daemon_data_tx_managed_flag);
    daemon_drain_framed_tx_completions(band->framed_log_ctx,
                                       client_data_framed_slots,
                                       MAX_CLIENTS);
    config_dispatch_context(config_ctx, MAX_CLIENTS, readfds, buf);
    daemon_flush_channel_logged(&channel, readfds, band->client_log_ctx);
}
