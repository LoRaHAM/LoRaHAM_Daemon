#ifndef LORAHAM_RADIO_TX_LIMIT_H
#define LORAHAM_RADIO_TX_LIMIT_H

#include <stddef.h>

#include "hardware_profile.h"  /* DaemonChipFamily */
#include "radio_channel.h"     /* RadioMode_t */

/* --- How many payload bytes one frame may carry -------------------------- */
/*
 * SX127x in FSK has a 64-byte FIFO, and variable-length packet mode puts the
 * length byte INTO that FIFO -- so the largest payload that fits is 63. Before
 * this rule the daemon accepted 255 in every mode, and an oversized FSK frame
 * went to the radio to fail there, or worse, partially.
 *
 * This is a RADIO-MODE constraint, and it is deliberately NOT expressed by
 * narrowing the three 255 constants. They are different things:
 *
 *   RF_PACKET_MAX_PAYLOAD_LEN  absolute buffer maximum (uint8_t send_buf[...])
 *   FRAMED_DATA_MAX_RF_PAYLOAD framing-layer storage ceiling, from which
 *                              FRAMED_DATA_RX_PAYLOAD_MAX derives
 *   DATA_TX_MAX_CHUNK_SIZE     the raw DATA chunker's ceiling
 *
 * Narrowing any of them to 63 would cripple LoRa and turn a radio constraint
 * into a wire-protocol constraint. They stay at 255; this rule says what the
 * radio will accept right now.
 *
 * The PURE form is the canonical one, and the reason is the CONFIG airtime
 * gate: it evaluates a PROSPECTIVE configuration. `SET MODE=FSK ...` is
 * validated while the controller still reports LoRa, so a helper that asked
 * the controller would compute the gate against the old 255 limit and let
 * through a configuration the daemon cannot actually send. CONFIG must pass
 * `validation.target_mode`, which config_apply.cpp already carries.
 */
size_t radio_tx_payload_limit(DaemonChipFamily family, RadioMode_t mode);

/* Thin live wrapper, for the paths that legitimately mean "right now".
 * A NULL controller or a controller without a driver answers with the
 * absolute maximum: this rule narrows, it never invents a limit where the
 * caller has no radio to ask. */
struct RadioController;
size_t radio_tx_payload_limit(const RadioController *ctrl);

#endif
