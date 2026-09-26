#include "daemon_data_tx_runtime.h"

#include "radio_tx_limit.h"

#include "rf_packet.h"

#include "daemon_band.h"

#include "daemon_log.h"

/* --- DATA TX logging ----------------------------------------------------- */

void daemon_data_tx_trace_message(void *ctx, const char *msg)
{
    daemon_debug_ctx((const char *)ctx, "%s", msg);
}

DataTxLog daemon_data_tx_log(const char *ctx)
{
    DataTxLog log = {
        (void *)ctx,
        daemon_data_tx_trace_message
    };

    return log;
}

/* --- Bodies moved verbatim from daemon_data_tx_runtime.h --- */

void data_tx_apply_cad_decision_flags(DaemonTxJob *job,
                                                    int decision)
{
    if (!job)
        return;

    if (data_tx_cad_wait_timed_out(decision))
        job->flags |= FRAMED_DATA_TX_RESULT_FLAG_CAD_TIMEOUT;
}

int daemon_data_tx_result_deferred(void *ctx)
{
    DataTxDaemonContext *tx =
        (DataTxDaemonContext *)ctx;

    return tx && tx->ctrl && tx->ctrl->tx_queue_active.load();
}

void daemon_data_tx_set_completion_target(void *ctx,
                                                 int slot_index,
                                                 uint32_t generation,
                                                 uint16_t seq)
{
    DataTxDaemonContext *tx =
        (DataTxDaemonContext *)ctx;

    if (!tx)
        return;

    tx->completion_slot = slot_index;
    tx->completion_generation = generation;
    tx->completion_seq = seq;
}

DataTxCadPolicy data_tx_snapshot_cad_policy(
    const RadioController *ctrl)
{
    DataTxCadPolicy p;

    if (!ctrl) {
        p.cad_wait_ticks        = daemon_tx_policy_cad_wait_ticks();
        p.cad_idle_stable_ticks = daemon_tx_policy_cad_idle_stable_ticks();
        p.cad_sleep_usec        = (useconds_t)daemon_tx_policy_poll_interval_usec();
        p.send_after_timeout    = DAEMON_TX_POLICY_SEND_AFTER_CAD_TIMEOUT ? true : false;
        return p;
    }

    uint32_t wait_ms = ctrl->cad_wait_timeout_ms.load();
    uint32_t idle_ms = ctrl->cad_idle_stable_ms.load();
    uint32_t poll_ms = ctrl->cad_poll_interval_ms.load();

    p.cad_wait_ticks        = daemon_tx_policy_ticks_for_ms(wait_ms, poll_ms);
    p.cad_idle_stable_ticks = daemon_tx_policy_ticks_for_ms(idle_ms, poll_ms);
    p.cad_sleep_usec        = (useconds_t)(poll_ms * 1000u);
    p.send_after_timeout    = ctrl->cad_send_after_timeout.load();
    return p;
}

/* Tri-state: the boolean reduction mapped UNAVAILABLE (scan
 * error) to "free" — a failed CAD operation must never gate a transmission
 * open. */
size_t data_tx_queue_capacity_bytes(void *ctx)
{
    DataTxDaemonContext *tx = (DataTxDaemonContext *)ctx;
    RadioController *ctrl = tx ? tx->ctrl : NULL;

    if (!ctrl || !ctrl->tx_queue_active.load())
        return (size_t)-1; /* direct path sends synchronously: no queue bound */

    size_t pending = daemon_tx_async_runtime_pending();

    if (pending >= DAEMON_TX_QUEUE_CAPACITY)
        return 0;

    /* The same limit the chunker uses, or the two disagree: 63-byte chunks
     * counted against 255-byte slots let one read produce about four times
     * as many jobs as there is room for. */
    return (DAEMON_TX_QUEUE_CAPACITY - pending) * radio_tx_payload_limit(ctrl);
}

/* The largest payload the radio will accept right now (data_tx.h's
 * DataTxChunkLimitFn). Paired with data_tx_queue_capacity_bytes() above. */
size_t data_tx_chunk_limit_bytes(void *ctx)
{
    DataTxDaemonContext *tx = (DataTxDaemonContext *)ctx;

    return radio_tx_payload_limit(tx ? tx->ctrl : NULL);
}

RadioCadProbeStatus data_tx_probe_channel_state(DataTxDaemonContext *tx)
{
    RadioController *ctrl = tx ? tx->ctrl : NULL;

    if (!ctrl || ctrl->mode != RADIO_MODE_LORA)
        return RADIO_CAD_PROBE_FREE;

    return radio_cad_probe(ctrl).status;
}

int data_tx_wait_channel_free_with_limits_ex(
    DataTxDaemonContext *tx,
    uint32_t max_ticks,
    uint32_t stable_idle_ticks,
    useconds_t sleep_usec,
    bool send_after_timeout)
{
    RadioController *ctrl = tx ? tx->ctrl : NULL;
    uint32_t cad_wait = 0;
    uint32_t free_ticks = 0;
    uint32_t required_free_ticks = stable_idle_ticks ? stable_idle_ticks : 1u;

    if (!ctrl || ctrl->mode != RADIO_MODE_LORA)
        return DATA_TX_CAD_WAIT_FREE;

    if (ctrl->tx_mode == RADIO_TX_MODE_DIRECT) {
        // DIRECT: transmit immediately, no CAD probe, no wait, no block.
        return DATA_TX_CAD_WAIT_FREE;
    }

    while (cad_wait < max_ticks) {
        RadioCadProbeStatus probe_status = data_tx_probe_channel_state(tx);

        if (probe_status == RADIO_CAD_PROBE_UNAVAILABLE)
            return DATA_TX_CAD_WAIT_ERROR;

        /*
         * A packet of OUR OWN is waiting to be read. Waiting cannot clear it:
         * this loop runs on the main thread, which is also the thread that
         * drains RX -- and it drains AFTER socket processing, so the packet
         * cannot be collected until this call returns. Every further probe
         * would see the same pending packet and report BUSY, burn the CAD
         * budget, and end in the timeout decision.
         *
         * That is the dangerous part: with CADTXAFTERTIMEOUT=1 the timeout
         * then permits a send, and the TX preparation clears `received` and
         * the IRQ flags -- destroying a completed packet that no client ever
         * saw. The probe protects it and the caller then erased it.
         *
         * So a pending local packet ends the attempt at once and never reaches
         * the timeout opt-in. BLOCK is the honest answer: the channel just
         * delivered something, we decline to transmit over it, and returning
         * now is what lets the main loop drain it.
         */
        if (ctrl->received.load())
            return DATA_TX_CAD_WAIT_BLOCK;

        if (probe_status != RADIO_CAD_PROBE_BUSY) {
            free_ticks++;
            if (free_ticks >= required_free_ticks)
                return DATA_TX_CAD_WAIT_FREE;
        } else {
            free_ticks = 0;
        }

        cad_wait++;
        if (sleep_usec > 0 && cad_wait < max_ticks)
            usleep(sleep_usec);
    }

    return send_after_timeout ?
           DATA_TX_CAD_WAIT_TIMEOUT_SEND :
           DATA_TX_CAD_WAIT_BLOCK;
}

int data_tx_wait_channel_free(DataTxDaemonContext *tx)
{
    RadioController *ctrl = tx ? tx->ctrl : NULL;
    DataTxCadPolicy p = data_tx_snapshot_cad_policy(ctrl);
    return data_tx_wait_channel_free_with_limits_ex(
        tx, p.cad_wait_ticks, p.cad_idle_stable_ticks,
        p.cad_sleep_usec, p.send_after_timeout);
}

int data_tx_wait_tx_ready_with_limits(RadioController *ctrl,
                                             uint32_t max_ticks,
                                             useconds_t sleep_usec)
{
    uint32_t tx_wait = 0;

    if (!ctrl)
        return 1;

    while (ctrl->tx_busy.load()) {
        if (tx_wait >= max_ticks)
            return 1;

        tx_wait++;
        if (sleep_usec > 0 && tx_wait < max_ticks)
            usleep(sleep_usec);
    }

    return 0;
}

int data_tx_wait_tx_ready(RadioController *ctrl)
{
    return data_tx_wait_tx_ready_with_limits(
        ctrl,
        daemon_tx_policy_busy_timeout_ticks(),
        (useconds_t)daemon_tx_policy_poll_interval_usec());
}

int daemon_data_tx_result_enabled(void *ctx)
{
    DataTxDaemonContext *tx =
        (DataTxDaemonContext *)ctx;

    return tx && tx->ctrl && tx->ctrl->tx_result_active.load();
}

uint8_t daemon_data_tx_managed_flag(void *ctx)
{
    DataTxDaemonContext *tx =
        (DataTxDaemonContext *)ctx;

    if (tx && tx->ctrl && tx->ctrl->tx_mode == RADIO_TX_MODE_MANAGED)
        return FRAMED_DATA_TX_RESULT_FLAG_MANAGED;

    return 0;
}

uint16_t daemon_data_tx_next_result_seq(void *ctx)
{
    DataTxDaemonContext *tx =
        (DataTxDaemonContext *)ctx;

    if (!tx || !tx->ctrl)
        return 0;

    tx->ctrl->tx_result_seq++;
    return tx->ctrl->tx_result_seq;
}

int daemon_data_tx_worker_cad_probe(int band, void *ctx)
{
    RadioController *ctrl = (RadioController *)ctx;
    RadioCadProbeResult probe;

    if (!ctrl || band != radio_controller_band_number(ctrl))
        return DAEMON_TX_CAD_PROBE_UNAVAILABLE;

    std::lock_guard<std::recursive_mutex> radio_lock(ctrl->radio_mutex);

    if (!radio_controller_ready(ctrl))
        return DAEMON_TX_CAD_PROBE_UNAVAILABLE;

    if (ctrl->mode != RADIO_MODE_LORA)
        return DAEMON_TX_CAD_PROBE_FREE;

    probe = radio_cad_probe(ctrl);
    if (probe.status == RADIO_CAD_PROBE_FREE)
        return DAEMON_TX_CAD_PROBE_FREE;

    if (probe.status == RADIO_CAD_PROBE_BUSY)
        return DAEMON_TX_CAD_PROBE_BUSY;

    return DAEMON_TX_CAD_PROBE_UNAVAILABLE;
}

void daemon_data_tx_worker_cad_sleep(uint32_t usec, void *ctx)
{
    (void)ctx;

    if (usec > 0)
        usleep((useconds_t)usec);
}

void daemon_data_tx_configure_worker_cad(DaemonTxAsyncWorker *async,
                                                DataTxDaemonContext *tx,
                                                const DaemonTxJob *job)
{
    if (!async)
        return;

    (void)job;

    if (!tx || !tx->ctrl) {
        daemon_tx_async_worker_configure_cad(async, NULL, NULL, NULL, NULL);
        return;
    }

    daemon_tx_async_worker_configure_cad(async,
                                         daemon_data_tx_worker_cad_probe,
                                         tx->ctrl,
                                         daemon_data_tx_worker_cad_sleep,
                                         NULL);
}

void data_tx_configure_job_cad_policy(DataTxDaemonContext *tx,
                                             DaemonTxJob *job)
{
    RadioController *ctrl = tx ? tx->ctrl : NULL;

    if (!tx || !job || !ctrl || ctrl->mode != RADIO_MODE_LORA) {
        daemon_tx_job_configure_cad_policy(job, 0, 0, 0, 0, 0);
        return;
    }

    if (ctrl->tx_mode == RADIO_TX_MODE_DIRECT) {
        // DIRECT: disable CAD entirely so the worker transmits immediately.
        daemon_tx_job_configure_cad_policy(job, 0, 0, 0, 0, 0);
        return;
    }

    DataTxCadPolicy cad_p = data_tx_snapshot_cad_policy(ctrl);
    daemon_tx_job_configure_cad_policy(job,
                                       1,
                                       cad_p.cad_wait_ticks,
                                       cad_p.cad_idle_stable_ticks,
                                       (uint32_t)cad_p.cad_sleep_usec,
                                       cad_p.send_after_timeout ? 1 : 0);
}

DaemonTxJobResult daemon_data_tx_execute_job(DataTxDaemonContext *tx,
                                                    const DaemonTxJob *job,
                                                    DaemonTxSendFn send_fn)
{
    DaemonTxJobResult result;
    DaemonTxAsyncWorker *async;
    DaemonTxCompletionQueue *completion_queue;
    void *completion_record_ctx;

    daemon_tx_job_result_init(&result,
                              job,
                              DAEMON_TX_OUTCOME_RADIO_ERROR);

    if (!tx || !tx->ctrl || !job)
        return result;

    if (!tx->ctrl->tx_queue_active.load())
        return daemon_tx_execute_job_with_sender(job, send_fn, tx->send_ctx);

    async = daemon_tx_async_runtime_worker();
    completion_queue = daemon_tx_async_runtime_completion_queue();
    completion_record_ctx =
        daemon_tx_async_runtime_completion_record_ctx();
    if (!async || !completion_queue || !completion_record_ctx)
        return result;

    daemon_tx_async_runtime_set_stats(&tx->ctrl->stats);

    daemon_tx_async_worker_configure(async,
                                     send_fn,
                                     tx->send_ctx,
                                     daemon_tx_async_runtime_record_completion,
                                     completion_record_ctx);
    daemon_data_tx_configure_worker_cad(async, tx, job);

    if (daemon_tx_async_worker_start(async) != 0)
        return result;

    if (daemon_tx_async_worker_submit(async, job) != 0) {
        daemon_tx_job_result_init(&result,
                                  job,
                                  DAEMON_TX_OUTCOME_CHANNEL_BUSY);
        result.tx_result = TX_RESULT_BUSY;
        return result;
    }

    daemon_tx_job_result_init(&result,
                              job,
                              DAEMON_TX_OUTCOME_OK);
    result.tx_result = TX_RESULT_OK;
    return result;
}

int send_data_chunk(uint8_t *chunk, size_t len, size_t offset, void *ctx)
{
    DataTxDaemonContext *tx = (DataTxDaemonContext *)ctx;
    RadioController *ctrl = tx->ctrl;
    const char *tag = radio_controller_tag(ctrl);
    int band = radio_controller_band_number(ctrl);

    if (!radio_controller_ready(ctrl)) {
        daemon_debug_ctx(tx->log_ctx, "radio not ready");
        printf("[%s] DATA TX aborted: RADIO_NOT_READY\n", tag);
        return DAEMON_TX_OUTCOME_RADIO_NOT_READY;
    }

    /* Payload limit, before ANY queue or radio mutation. The raw path chunks
     * to this limit and never arrives here oversized, but a framed client
     * sends its own length: a 64-byte TX_PACKET parses correctly into the
     * 255-byte frame buffer and only the radio's FIFO says it is too big. It
     * is rejected here instead, so no job is queued and the chip is not
     * touched. */
    size_t limit = radio_tx_payload_limit(ctrl);

    if (len > limit) {
        daemon_radio_stats_record_tx_result(&ctrl->stats,
                                            TX_RESULT_INVALID_PACKET);
        daemon_debug_ctx(tx->log_ctx, "payload too large");
        printf("[%s] DATA TX aborted: %s (%zu bytes > %zu in mode %s)\n",
               tag, tx_result_name(TX_RESULT_INVALID_PACKET), len, limit,
               radio_mode_name(ctrl->mode));
        return DAEMON_TX_OUTCOME_INVALID_PACKET;
    }

    /* Defense in depth: with the queue disabled but the
     * worker still holding or executing residual jobs, a direct TX must
     * neither wait behind nor interleave with them — reject as BUSY. */
    if (!ctrl->tx_queue_active.load() &&
        (daemon_tx_async_runtime_pending() > 0 ||
         daemon_tx_async_runtime_job_active())) {
        daemon_radio_stats_record_tx_result(&ctrl->stats, TX_RESULT_BUSY);
        daemon_debug_ctx(tx->log_ctx, "residual queue active");
        printf("[%s] DATA TX aborted: %s (residual queue active)\n", tag,
               tx_result_name(TX_RESULT_BUSY));
        return DAEMON_TX_OUTCOME_BUSY;
    }

    if (!ctrl->tx_queue_active.load() &&
        data_tx_wait_tx_ready_with_limits(ctrl,
                                          tx->tx_busy_wait_ticks,
                                          tx->tx_busy_sleep_usec)) {
        daemon_radio_stats_record_tx_result(&ctrl->stats, TX_RESULT_BUSY);
        daemon_debug_ctx(tx->log_ctx, "TX busy");
        printf("[%s] DATA TX aborted: %s\n", tag,
               tx_result_name(TX_RESULT_BUSY));
        return DAEMON_TX_OUTCOME_CHANNEL_BUSY;
    }

    int queued_tx = ctrl->tx_queue_active.load();
    int cad_decision = DATA_TX_CAD_WAIT_FREE;

    // CAD guard: LoRa only. Queued TX runs CAD in the worker.
    if (ctrl->mode == RADIO_MODE_LORA) {
        if (queued_tx)
            daemon_debug_ctx(tx->log_ctx, "CAD is checked in the worker");
        else
            daemon_debug_ctx(tx->log_ctx, "checking CAD");
    }

    if (!queued_tx) {
        cad_decision = data_tx_wait_channel_free(tx);

        if (cad_decision == DATA_TX_CAD_WAIT_ERROR) {
            TxResult err_result = TX_RESULT_RADIO_ERROR;
            daemon_radio_stats_record_tx_result(&ctrl->stats, err_result);
            daemon_debug_ctx(tx->log_ctx, "CAD probe failed");
            printf("[%s] DATA TX aborted: %s (CAD UNAVAILABLE)\n", tag,
                   tx_result_name(err_result));
            return DAEMON_TX_OUTCOME_RADIO_ERROR;
        }

        if (data_tx_cad_wait_blocks_tx(cad_decision)) {
            // Only MANAGED can block now; DIRECT never reaches this path.
            TxResult busy_result = TX_RESULT_CAD_TIMEOUT;
            daemon_radio_stats_record_tx_result(&ctrl->stats, busy_result);
            daemon_debug_ctx(tx->log_ctx, "channel busy");
            printf("[%s] channel busy, packet discarded\n", tag);
            printf("[%s] DATA TX aborted: %s\n", tag,
                   tx_result_name(busy_result));
            return DAEMON_TX_OUTCOME_CHANNEL_BUSY;
        }

        if (data_tx_cad_wait_timed_out(cad_decision)) {
            daemon_radio_stats_record_cad_timeout_send(&ctrl->stats);
            daemon_debug_ctx(tx->log_ctx, "CAD timeout, sending anyway");
        }
    }

    daemon_debug_ctx(tx->log_ctx, "chunk %zu bytes offset %zu", len, offset);

    DaemonTxJob job;
    DaemonTxJobResult result;
    DaemonTxSendFn send_fn = tx->send_fn ?
                             tx->send_fn :
                             daemon_tx_executor_default_send;

    daemon_tx_job_init(&job, band, ctrl->tx_mode, tx->completion_seq);
    job.completion_slot = tx->completion_slot;
    job.completion_generation = tx->completion_generation;
    // Only MANAGED performs CAD/LBT; DIRECT sends immediately, so its result
    // must not advertise the managed flag.
    job.flags = (ctrl->tx_mode == RADIO_TX_MODE_MANAGED)
                    ? FRAMED_DATA_TX_RESULT_FLAG_MANAGED
                    : 0;
    if (queued_tx)
        data_tx_configure_job_cad_policy(tx, &job);
    data_tx_apply_cad_decision_flags(&job, cad_decision);
    if (daemon_tx_job_set_payload(&job, chunk, len) != 0) {
        daemon_debug_ctx(tx->log_ctx, "aborted: INVALID_PACKET");
        printf("[%s] DATA TX aborted: INVALID_PACKET\n", tag);
        return DAEMON_TX_OUTCOME_INVALID_PACKET;
    }

    result = daemon_data_tx_execute_job(tx, &job, send_fn);
    if (!queued_tx || daemon_tx_outcome_is_failure(result.outcome))
        daemon_radio_stats_record_tx_result(&ctrl->stats, result.tx_result);

    if (daemon_tx_outcome_is_failure(result.outcome)) {
        daemon_debug_ctx(tx->log_ctx, "aborted: %s",
                         tx_result_name(result.tx_result));
        printf("[%s] DATA TX aborted: %s\n", tag,
               tx_result_name(result.tx_result));
        return result.outcome;
    }

    daemon_debug_ctx(tx->log_ctx, "chunk sent");
    return 0;
}

DataTxDaemonContext daemon_data_tx_context(RadioController *ctrl)
{
    const char *log_ctx = daemon_band()->tx_log_ctx;

    DataTxDaemonContext ctx = {
        ctrl,
        log_ctx,
        daemon_tx_executor_default_send,
        NULL,
        DAEMON_TX_COMPLETION_SLOT_NONE,
        0,
        0u,
        daemon_tx_policy_busy_timeout_ticks(),
        (useconds_t)daemon_tx_policy_poll_interval_usec(),
        daemon_tx_policy_cad_wait_ticks(),
        daemon_tx_policy_cad_idle_stable_ticks(),
        (useconds_t)daemon_tx_policy_poll_interval_usec()
    };

    return ctx;
}
