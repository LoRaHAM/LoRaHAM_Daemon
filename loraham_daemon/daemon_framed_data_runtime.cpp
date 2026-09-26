#include "daemon_framed_data_runtime.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "client_output_queue.h"
#include "daemon_log.h"
#include "daemon_tx_async_runtime.h"
#include "daemon_tx_completion.h"
#include "daemon_tx_outcome.h"
#include "framed_data.h"

/* --- Framed DATA runtime helpers ---------------------------------------- */

int daemon_framed_tx_should_emit_immediate_result(int result_active,
                                                  int result_deferred,
                                                  int handler_result)
{
    if (!result_active)
        return 0;

    if (!result_deferred)
        return 1;

    return daemon_tx_outcome_is_failure(handler_result);
}


static int daemon_queue_framed_tx_result(ClientSlot *slot,
                                         uint8_t status,
                                         uint8_t flags,
                                         uint16_t seq)
{
    uint8_t frame[FRAMED_DATA_HEADER_LEN + FRAMED_DATA_TX_RESULT_PAYLOAD_LEN];

    if (!slot)
        return -1;

    if (framed_data_encode_tx_result(frame,
                                     sizeof(frame),
                                     status,
                                     flags,
                                     seq) != 0)
        return -1;

    if (!client_output_queue_append(&slot->output, frame, sizeof(frame)))
        return -1;

    return 0;
}

static int daemon_queue_framed_error(ClientSlot *slot, const char *msg)
{
    uint8_t header[FRAMED_DATA_HEADER_LEN];
    size_t len;

    if (!slot || !msg)
        return -1;

    len = strlen(msg);
    if (len > 255)
        len = 255;

    if (framed_data_encode_header(header, sizeof(header),
                                  FRAMED_DATA_TYPE_ERROR,
                                  (uint16_t)len) != 0)
        return -1;

    if (!client_output_queue_append(&slot->output, header, sizeof(header)))
        return -1;

    if (len > 0 &&
        !client_output_queue_append(&slot->output,
                                    (const uint8_t *)msg,
                                    len))
        return -1;

    return 0;
}

static uint8_t daemon_framed_tx_status_from_handler_result(int result)
{
    return daemon_tx_outcome_to_framed_status(result);
}

typedef struct {
    ClientSlot *slot;
    const char *tag;
} DaemonFramedErrorContext;

typedef struct {
    ClientSlot *slot;
    const char *tag;
    FramedDataTxPacketHandler handler;
    void *handler_ctx;
    DaemonFramedTxResultEnabledFn result_enabled;
    DaemonFramedTxNextSeqFn next_seq;
    DaemonFramedTxResultDeferredFn result_deferred;
    DaemonFramedTxTargetFn set_target;
    DaemonFramedTxManagedFlagFn managed_flag;
    int slot_index;
} DaemonFramedTxContext;

static int daemon_framed_tx_packet(uint8_t *payload, size_t len, void *ctx)
{
    DaemonFramedTxContext *tx = (DaemonFramedTxContext *)ctx;
    int result_active;
    uint16_t seq = 0;
    int result = -1;
    int deferred_result = 0;

    if (!tx)
        return -1;

    result_active = tx->result_enabled &&
                    tx->result_enabled(tx->handler_ctx);
    if (result_active && tx->next_seq)
        seq = tx->next_seq(tx->handler_ctx);

    deferred_result = tx->result_deferred &&
                      tx->result_deferred(tx->handler_ctx);

    if (tx->set_target)
        tx->set_target(tx->handler_ctx,
                       tx->slot_index,
                       client_slot_generation(tx->slot),
                       seq);

    if (tx->handler)
        result = tx->handler(payload, len, tx->handler_ctx);

    if (tx->set_target)
        tx->set_target(tx->handler_ctx, DAEMON_TX_COMPLETION_SLOT_NONE, 0u, 0);

    if (daemon_framed_tx_should_emit_immediate_result(result_active,
                                                        deferred_result,
                                                        result)) {
        uint8_t status = daemon_framed_tx_status_from_handler_result(result);
        uint8_t flags = tx->managed_flag
                            ? tx->managed_flag(tx->handler_ctx)
                            : FRAMED_DATA_TX_RESULT_FLAG_MANAGED;

        if (daemon_queue_framed_tx_result(tx->slot,
                                          status,
                                          flags,
                                          seq) != 0) {
            daemon_debug_ctx(tx->tag ? tx->tag : "TXF",
                             "TX_RESULT queue failed, closing client");
            if (tx->slot)
                client_slot_close(tx->slot);
        }

        return 0;
    }

    if (result_active)
        return 0;

    return result;
}

static void daemon_framed_tx_error(const char *msg, void *ctx)
{
    DaemonFramedErrorContext *err = (DaemonFramedErrorContext *)ctx;

    if (!err || !err->slot)
        return;

    daemon_debug_ctx(err->tag ? err->tag : "TXF",
                     "frame error: %s", msg ? msg : "");
    if (daemon_queue_framed_error(err->slot, msg ? msg : "frame error") != 0)
        client_slot_close(err->slot);
}

void daemon_process_framed_data_slots(const char *tag,
                                      ClientSlot *slots,
                                      FramedDataTxState *states,
                                      int max_clients,
                                      const EventLoopReadySet *readfds,
                                      FramedDataTxPacketHandler handler,
                                      void *ctx,
                                      DaemonFramedTxResultEnabledFn result_enabled,
                                      DaemonFramedTxNextSeqFn next_seq,
                                      DaemonFramedTxResultDeferredFn result_deferred,
                                      DaemonFramedTxTargetFn set_target,
                                      DaemonFramedTxManagedFlagFn managed_flag)
{
    if (!slots || !states)
        return;

    for (int i = 0; i < max_clients; i++) {
        ClientSlot *slot = &slots[i];

        if (!client_slot_has_client(slot)) {
            framed_data_tx_state_init(&states[i]);
            continue;
        }

        if (!client_slot_ready(slot, readfds))
            continue;

        uint8_t buf[512];
        ssize_t n;

        do {
            n = read(slot->fd, buf, sizeof(buf));
        } while (n < 0 && errno == EINTR);

        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                continue;

            daemon_debug_ctx(tag, "read error, closing client");
            framed_data_tx_state_init(&states[i]);
            client_slot_close(slot);
            continue;
        }

        if (n == 0) {
            daemon_debug_ctx(tag, "EOF, closing client");
            framed_data_tx_state_init(&states[i]);
            client_slot_close(slot);
            continue;
        }

        DaemonFramedErrorContext err = { slot, tag };
        DaemonFramedTxContext tx = {
            slot,
            tag,
            handler,
            ctx,
            result_enabled,
            next_seq,
            result_deferred,
            set_target,
            managed_flag,
            i
        };

        daemon_debug_ctx(tag, "framed DATA: %zd bytes received", n);
        if (framed_data_tx_feed_state(&states[i],
                                      buf,
                                      (size_t)n,
                                      daemon_framed_tx_packet,
                                      &tx,
                                      daemon_framed_tx_error,
                                      &err) != 0) {
            daemon_debug_ctx(tag, "TX handler error, closing client");
            framed_data_tx_state_init(&states[i]);
            client_slot_close(slot);
        }
    }
}


void daemon_drain_framed_tx_completions(const char *tag,
                                        ClientSlot *slots,
                                        int max_clients)
{
    DaemonTxJobResult result;

    if (!slots || max_clients <= 0)
        return;

    while (daemon_tx_async_runtime_pop_completion(&result) == 0) {
        int rc = daemon_tx_completion_deliver_to_slot(slots,
                                                      max_clients,
                                                      &result);

        if (rc == DAEMON_TX_COMPLETION_DELIVERY_STALE) {
            daemon_tx_async_runtime_record_completion_stale();
            daemon_debug_ctx(tag ? tag : "TXF",
                             "TX completion stale, dropped");
            continue;
        }

        if (rc < 0) {
            daemon_debug_ctx(tag ? tag : "TXF",
                             "TX completion delivery failed");
        }
    }
}

