#ifndef LORAHAM_DAEMON_FRAMED_DATA_RUNTIME_H
#define LORAHAM_DAEMON_FRAMED_DATA_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "client_slot.h"
#include "daemon_data_tx_runtime.h"
#include "daemon_tx_completion.h"
#include "event_loop.h"
#include "framed_data_tx.h"

/* --- Framed DATA runtime helpers ---------------------------------------- */

int daemon_framed_tx_should_emit_immediate_result(int result_active,
                                                  int result_deferred,
                                                  int handler_result);

typedef int (*DaemonFramedTxResultEnabledFn)(void *ctx);
typedef uint16_t (*DaemonFramedTxNextSeqFn)(void *ctx);
typedef int (*DaemonFramedTxResultDeferredFn)(void *ctx);
typedef void (*DaemonFramedTxTargetFn)(void *ctx, int slot_index, uint32_t generation, uint16_t seq);
typedef uint8_t (*DaemonFramedTxManagedFlagFn)(void *ctx);

static inline int send_framed_data_packet(uint8_t *payload,
                                          size_t len,
                                          void *ctx)
{
    return send_data_chunk(payload, len, 0, ctx);
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
                                      DaemonFramedTxManagedFlagFn managed_flag);

void daemon_drain_framed_tx_completions(const char *tag,
                                        ClientSlot *slots,
                                        int max_clients);

#endif
