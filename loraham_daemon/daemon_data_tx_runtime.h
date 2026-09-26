#ifndef LORAHAM_DAEMON_DATA_TX_RUNTIME_H
#define LORAHAM_DAEMON_DATA_TX_RUNTIME_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "data_tx.h"
#include "daemon_log.h"
#include "daemon_radio_runtime.h"
#include "daemon_stats.h"
#include "daemon_tx.h"
#include "daemon_tx_async_runtime.h"
#include "radio_cad.h"
#include "daemon_tx_executor.h"
#include "daemon_tx_outcome.h"
#include "daemon_tx_policy.h"
#include "radio_cad.h"
#include "radio_controller.h"
#include "tx_result.h"

/* --- DATA TX runtime helpers -------------------------------------------- */
#define DATA_TX_CAD_WAIT_FREE 0
#define DATA_TX_CAD_WAIT_BLOCK 1
#define DATA_TX_CAD_WAIT_TIMEOUT_SEND 2
/* Probe reported UNAVAILABLE (scan error / radio state untouchable): never
 * treated as free — the TX aborts as RADIO_ERROR, mirroring the queued
 * executor. */
#define DATA_TX_CAD_WAIT_ERROR 3

static inline int data_tx_cad_wait_blocks_tx(int decision)
{
    return decision == DATA_TX_CAD_WAIT_BLOCK;
}

static inline int data_tx_cad_wait_timed_out(int decision)
{
    return decision == DATA_TX_CAD_WAIT_TIMEOUT_SEND;
}

void data_tx_apply_cad_decision_flags(DaemonTxJob *job,
                                                    int decision);

struct DataTxDaemonContext {
    RadioController *ctrl;
    const char *log_ctx;
    DaemonTxSendFn send_fn;
    void *send_ctx;
    int completion_slot;
    uint16_t completion_seq;
    uint32_t completion_generation;
    uint32_t tx_busy_wait_ticks;
    useconds_t tx_busy_sleep_usec;
    uint32_t cad_wait_ticks;
    uint32_t cad_idle_stable_ticks;
    useconds_t cad_sleep_usec;
};

void daemon_data_tx_trace_message(void *ctx, const char *msg);
DataTxLog daemon_data_tx_log(const char *ctx);


int daemon_data_tx_result_deferred(void *ctx);

void daemon_data_tx_set_completion_target(void *ctx,
                                                 int slot_index,
                                                 uint32_t generation,
                                                 uint16_t seq);

struct DataTxCadPolicy {
    uint32_t   cad_wait_ticks;
    uint32_t   cad_idle_stable_ticks;
    useconds_t cad_sleep_usec;
    bool       send_after_timeout;
};

DataTxCadPolicy data_tx_snapshot_cad_policy(
    const RadioController *ctrl);

int data_tx_probe_channel_busy(DataTxDaemonContext *tx);

RadioCadProbeStatus data_tx_probe_channel_state(DataTxDaemonContext *tx);

/* Raw-DATA read budget: with the async queue active, one 2048-
 * byte read chunks into up to 9 jobs but the queue holds 8 — consumed bytes
 * past capacity were silently dropped. */
size_t data_tx_queue_capacity_bytes(void *ctx);
size_t data_tx_chunk_limit_bytes(void *ctx);

int data_tx_wait_channel_free_with_limits_ex(
    DataTxDaemonContext *tx,
    uint32_t max_ticks,
    uint32_t stable_idle_ticks,
    useconds_t sleep_usec,
    bool send_after_timeout);

static inline int data_tx_wait_channel_free_with_limits(DataTxDaemonContext *tx,
                                                 uint32_t max_ticks,
                                                 uint32_t stable_idle_ticks,
                                                 useconds_t sleep_usec)
{
    return data_tx_wait_channel_free_with_limits_ex(
        tx, max_ticks, stable_idle_ticks, sleep_usec, false);
}

int data_tx_wait_channel_free(DataTxDaemonContext *tx);

int data_tx_wait_tx_ready_with_limits(RadioController *ctrl,
                                             uint32_t max_ticks,
                                             useconds_t sleep_usec);

int data_tx_wait_tx_ready(RadioController *ctrl);

int daemon_data_tx_result_enabled(void *ctx);

// Managed flag for an immediate framed TX_RESULT: set only in MANAGED mode
// (DIRECT sends without CAD/LBT and must not advertise the managed flag).
uint8_t daemon_data_tx_managed_flag(void *ctx);

uint16_t daemon_data_tx_next_result_seq(void *ctx);


int daemon_data_tx_worker_cad_probe(int band, void *ctx);

void daemon_data_tx_worker_cad_sleep(uint32_t usec, void *ctx);

void daemon_data_tx_configure_worker_cad(DaemonTxAsyncWorker *async,
                                                DataTxDaemonContext *tx,
                                                const DaemonTxJob *job);

void data_tx_configure_job_cad_policy(DataTxDaemonContext *tx,
                                             DaemonTxJob *job);

DaemonTxJobResult daemon_data_tx_execute_job(DataTxDaemonContext *tx,
                                                    const DaemonTxJob *job,
                                                    DaemonTxSendFn send_fn);

int send_data_chunk(uint8_t *chunk, size_t len, size_t offset, void *ctx);

DataTxDaemonContext daemon_data_tx_context(RadioController *ctrl);

#endif
