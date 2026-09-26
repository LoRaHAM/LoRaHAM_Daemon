#ifndef LORAHAM_DAEMON_TX_EXECUTOR_H
#define LORAHAM_DAEMON_TX_EXECUTOR_H

#include <stddef.h>
#include <stdint.h>

#include "daemon_tx.h"
#include "daemon_tx_job.h"
#include "daemon_tx_outcome.h"

/* --- TX-Ausfuehrung ----------------------------------------------- */

typedef enum {
    DAEMON_TX_CAD_PROBE_UNAVAILABLE = -1,
    DAEMON_TX_CAD_PROBE_FREE = 0,
    DAEMON_TX_CAD_PROBE_BUSY = 1
} DaemonTxCadProbeState;

typedef int (*DaemonTxCadProbeFn)(int band, void *ctx);
typedef void (*DaemonTxSleepFn)(uint32_t usec, void *ctx);

typedef TxResult (*DaemonTxSendFn)(uint8_t *payload,
                                   size_t len,
                                   int band,
                                   void *ctx);

static inline TxResult daemon_tx_executor_default_send(uint8_t *payload,
                                                       size_t len,
                                                       int band,
                                                       void *ctx)
{
    (void)ctx;

    return lora_send(payload, len, band);
}

DaemonTxJobResult daemon_tx_execute_job_with_sender(const DaemonTxJob *job,
                                                                 DaemonTxSendFn send_fn,
                                                                 void *send_ctx);

DaemonTxJobResult daemon_tx_execute_job_with_sender_and_cad(const DaemonTxJob *job,
                                                                          DaemonTxSendFn send_fn,
                                                                          void *send_ctx,
                                                                          DaemonTxCadProbeFn cad_probe_fn,
                                                                          void *cad_ctx,
                                                                          DaemonTxSleepFn sleep_fn,
                                                                          void *sleep_ctx);

static inline DaemonTxJobResult daemon_tx_execute_job(const DaemonTxJob *job)
{
    return daemon_tx_execute_job_with_sender(job,
                                            daemon_tx_executor_default_send,
                                            NULL);
}

#endif
