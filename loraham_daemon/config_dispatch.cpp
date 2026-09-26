#include "config_dispatch.h"

#include <ctype.h>

#include "daemon_rx_rearm.h"
#include "daemon_tx_async_runtime.h"
#include "config_parser.h"

/* Bodies moved verbatim from config_dispatch.h. */

/* Stable per-command CONF reply: exactly one newline-
 * terminated response per complete command, delivered only to the requesting
 * client. GET STATUS/STATS/CHANNEL and GET CHANNEL NOSCAN answer with their
 * data line instead and never get a trailing OK. Queue-append failure closes the client (existing
 * slow-client policy). */
static void config_dispatch_reply(ClientSlot *slot, const char *reply)
{
    if (!client_output_queue_append(&slot->output,
                                    (const uint8_t *)reply,
                                    strlen(reply))) {
        client_slot_close(slot);
        return;
    }
    client_slot_flush_output(slot);
}

void config_dispatch_log_bytes(const ConfigDispatchLog *log,
                                             ssize_t n)
{
    char msg[64];

    snprintf(msg, sizeof(msg), "%zd bytes received", n);
    config_dispatch_log_message(log, msg);
}

void config_dispatch_log_slot(const ConfigDispatchLog *log,
                                            int index,
                                            const char *msg)
{
    char line[80];

    snprintf(line, sizeof(line), "Slot %d: %s", index, msg);
    config_dispatch_log_message(log, line);
}

/*
 * LOCK DISCIPLINE EXCEPTION (deliberate): CONFIG apply may BLOCK on
 * radio_mutex while a TX is in flight.
 *
 * The main-loop rule (radio_controller.h) says main-loop paths must gate or
 * try-lock so the loop never stalls behind the worker's blocking transmit().
 * CONFIG apply is the conscious exception: it is client-initiated, rare, and
 * must not be silently dropped — a skipped SET/CONFIG would leave the client
 * believing a configuration it never got. Blocking here for the remainder of
 * one TX (bounded by the transmit airtime) is the correct trade-off; the
 * periodic ticks (RX poll, CAD monitor, GETRSSI) skip instead.
 */
void config_dispatch_apply_line(const char *line, void *user)
{
    ConfigLineApplyContext *ctx =
        (ConfigLineApplyContext *)user;

    config_dispatch_log_line(&ctx->log, "line", line);

    /* Reserved-setter matching is case-insensitive like every documented
     * CONF key: the dedicated matchers and the invalid-value classifier all
     * operate on an uppercased copy (values of reserved setters are numeric
     * or MANAGED/DIRECT, so uppercasing is loss-free). The generic CONFIG
     * path below keeps the raw line — its parser normalizes keys itself. */
    char upper_line[buf_SIZE];
    {
        size_t i = 0;

        for (; line[i] != '\0' && i < sizeof(upper_line) - 1; i++)
            upper_line[i] = (char)toupper((unsigned char)line[i]);
        upper_line[i] = '\0';
    }

    if(config_status_is_get_status(upper_line)) {
        char status[512];

        config_status_format(status, sizeof(status), ctx->ctrl);
        if(!client_output_queue_append(&ctx->slot->output,
                                       (const uint8_t *)status,
                                       strlen(status))) {
            client_slot_close(ctx->slot);
            return;
        }
        client_slot_flush_output(ctx->slot);
        return;
    }

    if(config_status_is_get_stats(upper_line)) {
        char stats[384];

        config_status_format_stats(stats, sizeof(stats), ctx->ctrl);
        if(!client_output_queue_append(&ctx->slot->output,
                                       (const uint8_t *)stats,
                                       strlen(stats))) {
            client_slot_close(ctx->slot);
            return;
        }
        client_slot_flush_output(ctx->slot);
        return;
    }

    /* NOSCAN first for readability only: `config_status_command_equals` compares the whole
     * trimmed line, so "GET CHANNEL" and "GET CHANNEL NOSCAN" are distinct and neither can
     * prefix-match the other whichever order they are tested in. */
    if(config_status_is_get_channel_noscan(upper_line)) {
        char channel[192];

        /* Answers the same line as GET CHANNEL but never scans the channel, so a client may poll
         * it without destroying frames that are arriving. Safe with an absent/unready ctrl. */
        config_status_format_channel_passive(channel, sizeof(channel), ctx->ctrl);
        if(!client_output_queue_append(&ctx->slot->output,
                                       (const uint8_t *)channel,
                                       strlen(channel))) {
            client_slot_close(ctx->slot);
            return;
        }
        client_slot_flush_output(ctx->slot);

        return;
    }

    if(config_status_is_get_channel(upper_line)) {
        char channel[192];

        config_status_format_channel(channel, sizeof(channel), ctx->ctrl);
        if(!client_output_queue_append(&ctx->slot->output,
                                       (const uint8_t *)channel,
                                       strlen(channel))) {
            client_slot_close(ctx->slot);
            return;
        }
        client_slot_flush_output(ctx->slot);

        return;
    }

    int txresult_enabled = 0;
    if(config_status_is_set_txresult(upper_line, &txresult_enabled)) {
        if(ctx->ctrl)
            ctx->ctrl->tx_result_active.store(txresult_enabled != 0);
        printf("[%s] TXRESULT=%d\n", ctx->tag, txresult_enabled ? 1 : 0);
        fflush(stdout);
        config_dispatch_reply(ctx->slot, "OK\n");
        return;
    }

    int txqueue_enabled = 0;
    if(config_status_is_set_txqueue(upper_line, &txqueue_enabled)) {
        /* Drain-before-disable: switching to the direct path
         * while the worker still holds or executes jobs would let new
         * direct CAD+TX stack behind active queued CAD+TX. Fail closed:
         * disabling requires an empty, idle queue. Enabling is always safe. */
        if (txqueue_enabled == 0 &&
            (daemon_tx_async_runtime_pending() > 0 ||
             daemon_tx_async_runtime_job_active())) {
            printf("[%s] TXQUEUE=0 rejected: queue active (%zu pending%s)\n",
                   ctx->tag, daemon_tx_async_runtime_pending(),
                   daemon_tx_async_runtime_job_active() ? ", 1 executing"
                                                        : "");
            fflush(stdout);
            config_dispatch_reply(ctx->slot, "ERR BUSY\n");
            return;
        }

        if(ctx->ctrl)
            ctx->ctrl->tx_queue_active.store(txqueue_enabled != 0);
        printf("[%s] TXQUEUE=%d\n", ctx->tag, txqueue_enabled ? 1 : 0);
        fflush(stdout);
        config_dispatch_reply(ctx->slot, "OK\n");
        return;
    }

    RadioTxMode_t txmode = RADIO_TX_MODE_MANAGED;
    if(config_status_is_set_txmode(upper_line, &txmode)) {
        if(ctx->ctrl)
            ctx->ctrl->tx_mode = txmode;
        printf("[%s] TXMODE=%s\n", ctx->tag, radio_tx_mode_name(txmode));
        fflush(stdout);
        config_dispatch_reply(ctx->slot, "OK\n");
        return;
    }

    int cadrssi_dbm = 0;
    if(config_status_is_set_cadrssi(upper_line, &cadrssi_dbm)) {
        if(ctx->ctrl)
            ctx->ctrl->cad_rssi_threshold_dbm.store((float)cadrssi_dbm);
        printf("[%s] CADRSSI=%d\n", ctx->tag, cadrssi_dbm);
        fflush(stdout);
        config_dispatch_reply(ctx->slot, "OK\n");
        return;
    }

    int cadmonitor_enabled = 0;
    if(config_status_is_set_cadmonitor(upper_line, &cadmonitor_enabled)) {
        if(ctx->ctrl) {
            ctx->ctrl->cad_monitor_active.store(cadmonitor_enabled != 0);
            ctx->ctrl->cad_monitor_free_streak.store(0);
            if(!cadmonitor_enabled)
                ctx->ctrl->cad_broadcast_active.store(false);
        }
        printf("[%s] CADMONITOR=%d\n", ctx->tag, cadmonitor_enabled ? 1 : 0);
        fflush(stdout);
        config_dispatch_reply(ctx->slot, "OK\n");
        return;
    }

    uint32_t cadwait_ms = 0;
    if(config_status_is_set_cadwait(upper_line, &cadwait_ms)) {
        if(ctx->ctrl)
            ctx->ctrl->cad_wait_timeout_ms.store(cadwait_ms);
        printf("[%s] CADWAIT=%u\n", ctx->tag, (unsigned)cadwait_ms);
        fflush(stdout);
        config_dispatch_reply(ctx->slot, "OK\n");
        return;
    }

    uint32_t cadidle_ms = 0;
    if(config_status_is_set_cadidle(upper_line, &cadidle_ms)) {
        if(ctx->ctrl)
            ctx->ctrl->cad_idle_stable_ms.store(cadidle_ms);
        printf("[%s] CADIDLE=%u\n", ctx->tag, (unsigned)cadidle_ms);
        fflush(stdout);
        config_dispatch_reply(ctx->slot, "OK\n");
        return;
    }

    uint32_t cadpoll_ms = 0;
    if(config_status_is_set_cadpoll(upper_line, &cadpoll_ms)) {
        if(ctx->ctrl)
            ctx->ctrl->cad_poll_interval_ms.store(cadpoll_ms);
        printf("[%s] CADPOLL=%u\n", ctx->tag, (unsigned)cadpoll_ms);
        fflush(stdout);
        config_dispatch_reply(ctx->slot, "OK\n");
        return;
    }

    int cadtx_val = 0;
    if(config_status_is_set_cadtxaftertimeout(upper_line, &cadtx_val)) {
        if(ctx->ctrl)
            ctx->ctrl->cad_send_after_timeout.store(cadtx_val != 0);
        printf("[%s] CADTXAFTERTIMEOUT=%d\n", ctx->tag, cadtx_val ? 1 : 0);
        fflush(stdout);
        config_dispatch_reply(ctx->slot, "OK\n");
        return;
    }

    /* Reserved runtime setters with bad values: the dedicated
     * matchers above only accept fully valid lines, so "SET CADWAIT=1" or
     * "SET TXMODE=foo" used to fall through to the generic path and answer
     * ERR UNKNOWN or ERR RADIO_NOT_READY. Classify them truthfully here,
     * independent of radio readiness — no radio access either way. */
    switch (config_status_classify_reserved_setter(upper_line)) {
    case 1:
        printf("[%s] CONFIG rejected: %s (invalid runtime-setter value)\n",
               ctx->tag, line);
        fflush(stdout);
        config_dispatch_reply(ctx->slot, "ERR INVALID\n");
        return;
    case 2:
        printf("[%s] CONFIG rejected: %s (incomplete runtime setter)\n",
               ctx->tag, line);
        fflush(stdout);
        config_dispatch_reply(ctx->slot, "ERR MALFORMED\n");
        return;
    default:
        break;
    }

    if(!ctx->ctrl || !ctx->ctrl->driver ||
       !radio_controller_ready(ctx->ctrl)) {
        config_dispatch_log_message(&ctx->log, "radio not ready");
        printf("[%s] RADIO=%s CONFIG ignored\n",
               ctx->tag,
               radio_health_name(radio_controller_health(ctx->ctrl)));
        fflush(stdout);
        config_dispatch_reply(ctx->slot, "ERR RADIO_NOT_READY\n");
        return;
    }

    /* Queue/config coordination: a queued TX job snapshots no
     * RF configuration — it transmits with whatever is configured at
     * execution time. Applying an RF change while jobs are pending or
     * executing would retune already-accepted packets, so such commands are
     * rejected until the queue is empty (queues drain in seconds; CONFIG is
     * client-initiated and can be retried). */
    if (config_command_touches_radio(line)) {
        size_t queued = daemon_tx_async_runtime_pending();
        int executing = daemon_tx_async_runtime_job_active();

        if (queued > 0 || executing) {
            config_dispatch_log_message(&ctx->log, "rejected: TX queue active");
            printf("[%s] CONFIG rejected: TX queue busy "
                   "(%zu pending%s) – retry when drained\n",
                   ctx->tag, queued, executing ? ", 1 executing" : "");
            fflush(stdout);
            config_dispatch_reply(ctx->slot, "ERR BUSY\n");
            return;
        }
    }

    config_dispatch_log_message(&ctx->log, "apply starting");

    ConfigApplyStatus apply_status;

    {
        std::lock_guard<std::recursive_mutex> radio_lock(ctx->ctrl->radio_mutex);

        apply_status = ctx->apply_config(*ctx->ctrl->driver, ctx->tag, line,
                                         ctx->ctrl->mode,
                                         ctx->ctrl->getrssi_active);

        /* Re-arm ONLY when the apply touched the radio: the
         * unconditional callback+startReceive here destroyed a received,
         * undrained packet even for unknown/rejected/no-op commands. */
        if (apply_status == CONFIG_APPLY_APPLIED) {
            // beginFSK()/begin() clears the IRQ callback.
            ctx->ctrl->driver->setPacketReceivedAction(ctx->ctrl->rx_callback);
            config_dispatch_log_message(&ctx->log, "callback reinstalled");
            daemon_rx_rearm_note_result(ctx->ctrl,
                                        ctx->ctrl->driver->startReceive(),
                                        "CONFIG");
        } else if (apply_status == CONFIG_APPLY_HW_ERROR) {
            /* Fail closed: a mid-apply RadioLib failure leaves the RF
             * configuration suspect — no last-known-good snapshot exists in
             * v112, so the radio goes FAILED (TX/CONFIG gate closed, GET
             * STATUS reports it) instead of pretending READY. */
            ctx->ctrl->health = RADIO_HEALTH_FAILED;
            printf("[%s] CONFIG hardware error: RADIO=FAILED (fail-closed)\n",
                   ctx->tag);
            fflush(stdout);
        }
    }

    if (apply_status == CONFIG_APPLY_APPLIED)
        config_dispatch_log_message(&ctx->log, "RX restarted");
    else
        config_dispatch_log_message(&ctx->log, "no RX restart (no apply)");

    switch (apply_status) {
    case CONFIG_APPLY_OK_NO_RADIO:
    case CONFIG_APPLY_APPLIED:
        config_dispatch_reply(ctx->slot, "OK\n");
        break;
    case CONFIG_APPLY_REJECTED_INVALID:
        config_dispatch_reply(ctx->slot, "ERR INVALID\n");
        break;
    case CONFIG_APPLY_REJECTED_MALFORMED:
        config_dispatch_reply(ctx->slot, "ERR MALFORMED\n");
        break;
    case CONFIG_APPLY_UNKNOWN:
        config_dispatch_reply(ctx->slot, "ERR UNKNOWN\n");
        break;
    case CONFIG_APPLY_HW_ERROR:
        config_dispatch_reply(ctx->slot, "ERR HARDWARE\n");
        break;
    }
}

void config_dispatch_client(ClientSlot *slots,
                                          int index,
                                          const EventLoopReadySet *readfds,
                                          uint8_t *buf,
                                          RadioController *ctrl,
                                          const char *tag,
                                          ConfigApplyFn apply_config,
                                          ConfigDispatchLog log)
{
    ClientSlot *slot = &slots[index];

    if(!client_slot_ready(slot, readfds))
        return;

    ConfigLineApplyContext line_ctx = {
        slot,
        ctrl,
        tag,
        apply_config,
        log
    };

    config_dispatch_log_slot(&log, index, "client ready");

    ssize_t n;

    do {
        n = read(slot->fd, buf, buf_SIZE - 1);
    } while(n < 0 && errno == EINTR);

    if(n < 0) {
        if(errno == EAGAIN || errno == EWOULDBLOCK)
            return;

        config_dispatch_log_slot(&log, index, "read error, closing client");
        client_slot_close(slot);
        return;
    }

    if(n == 0) {
        config_dispatch_log_slot(&log, index, "EOF, stream flush");
        if(config_stream_flush(&slot->stream,
                               config_dispatch_apply_line,
                               &line_ctx) != 0) {
            config_dispatch_log_slot(&log, index, "flush error");
            printf("[%s] CONFIG stream flush error\n", tag);
            fflush(stdout);
        }

        client_slot_close(slot);
        config_dispatch_log_slot(&log, index, "client closed");
        return;
    }

    config_dispatch_log_bytes(&log, n);

    if(config_stream_feed(&slot->stream, buf, (size_t)n,
                          config_dispatch_apply_line,
                          &line_ctx) != 0) {
        config_dispatch_log_slot(&log, index, "stream too long, closing client");
        printf("[%s] CONFIG stream too long, client closed\n", tag);
        fflush(stdout);
        client_slot_close(slot);
        return;
    }
}

void config_dispatch_clients(ClientSlot *slots,
                                           int max_clients,
                                           const EventLoopReadySet *readfds,
                                           uint8_t *buf,
                                           RadioController *ctrl,
                                           const char *tag,
                                           ConfigApplyFn apply_config,
                                           ConfigDispatchLog log)
{
    for(int i=0;i<max_clients;i++){
        config_dispatch_client(slots, i, readfds, buf,
                               ctrl, tag,
                               apply_config, log);
    }
}

void config_dispatch_context(ConfigDispatchContext *ctx,
                                           int max_clients,
                                           const EventLoopReadySet *readfds,
                                           uint8_t *buf)
{
    config_dispatch_clients(ctx->slots,
                            max_clients, readfds, buf,
                            ctx->ctrl, ctx->tag,
                            ctx->apply_config,
                            ctx->log);
}
