#ifndef LORAHAM_DAEMON_TX_JOB_H
#define LORAHAM_DAEMON_TX_JOB_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "daemon_tx_outcome.h"
#include "framed_data.h"

/* --- Interner TX-Auftrag ------------------------------------------------- */

#define DAEMON_TX_COMPLETION_SLOT_NONE -1

typedef struct {
    int band;
    int tx_mode;
    uint16_t seq;
    uint8_t flags;
    uint8_t cad_enabled;
    uint8_t cad_send_after_timeout;
    uint32_t cad_wait_ticks;
    uint32_t cad_idle_stable_ticks;
    uint32_t cad_poll_interval_usec;
    size_t payload_len;
    uint8_t payload[FRAMED_DATA_MAX_RF_PAYLOAD];
    int completion_slot;
    uint32_t completion_generation;
} DaemonTxJob;

typedef struct {
    uint16_t seq;
    uint8_t flags;
    DaemonTxOutcome outcome;
    TxResult tx_result;
    uint8_t framed_status;
    int completion_slot;
    uint32_t completion_generation;
} DaemonTxJobResult;

static inline void daemon_tx_job_init(DaemonTxJob *job,
                                      int band,
                                      int tx_mode,
                                      uint16_t seq)
{
    if (!job)
        return;

    memset(job, 0, sizeof(*job));
    job->band = band;
    job->tx_mode = tx_mode;
    job->seq = seq;
    job->cad_enabled = 0;
    job->cad_send_after_timeout = 0;
    job->cad_wait_ticks = 0;
    job->cad_idle_stable_ticks = 0;
    job->cad_poll_interval_usec = 0;
    job->completion_slot = DAEMON_TX_COMPLETION_SLOT_NONE;
    job->completion_generation = 0u;
}

static inline void daemon_tx_job_configure_cad_policy(DaemonTxJob *job,
                                                        int enabled,
                                                        uint32_t wait_ticks,
                                                        uint32_t idle_stable_ticks,
                                                        uint32_t poll_interval_usec,
                                                        int send_after_timeout)
{
    if (!job)
        return;

    job->cad_enabled = enabled ? 1u : 0u;
    job->cad_wait_ticks = wait_ticks;
    job->cad_idle_stable_ticks = idle_stable_ticks;
    job->cad_poll_interval_usec = poll_interval_usec;
    job->cad_send_after_timeout = send_after_timeout ? 1u : 0u;
}

static inline int daemon_tx_job_set_payload(DaemonTxJob *job,
                                            const uint8_t *payload,
                                            size_t len)
{
    if (!job || (!payload && len > 0))
        return -1;

    if (len > FRAMED_DATA_MAX_RF_PAYLOAD)
        return -1;

    job->payload_len = len;
    if (len > 0)
        memcpy(job->payload, payload, len);

    return 0;
}

static inline void daemon_tx_job_result_init(DaemonTxJobResult *result,
                                             const DaemonTxJob *job,
                                             DaemonTxOutcome outcome)
{
    if (!result)
        return;

    memset(result, 0, sizeof(*result));
    result->seq = job ? job->seq : 0;
    result->flags = job ? job->flags : 0;
    result->outcome = outcome;
    result->tx_result = daemon_tx_outcome_to_tx_result(outcome);
    result->framed_status = daemon_tx_outcome_to_framed_status(outcome);
    result->completion_slot = job ? job->completion_slot : DAEMON_TX_COMPLETION_SLOT_NONE;
    result->completion_generation = job ? job->completion_generation : 0u;
}

#endif
