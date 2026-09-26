#ifndef LORAHAM_FRAMED_DATA_TX_H
#define LORAHAM_FRAMED_DATA_TX_H

#include <stddef.h>
#include <stdint.h>

#include "framed_data.h"

/* --- Framed DATA TX stream state ---------------------------------------- */

typedef int (*FramedDataTxPacketHandler)(uint8_t *payload,
                                         size_t len,
                                         void *ctx);

typedef void (*FramedDataTxErrorHandler)(const char *msg, void *ctx);

typedef struct {
    uint8_t header[FRAMED_DATA_HEADER_LEN];
    size_t header_len;
    uint8_t payload[FRAMED_DATA_MAX_RF_PAYLOAD];
    size_t payload_len;
    uint16_t expected_len;
    uint8_t frame_type;
    /* Bytes of a rejected frame's declared payload still to swallow: an
     * invalid header yields exactly ONE error and the parser re-syncs after
     * the declared length instead of parsing payload bytes as headers. */
    uint16_t skip_len;
} FramedDataTxState;

void framed_data_tx_state_init(FramedDataTxState *state);
void framed_data_tx_state_init_all(FramedDataTxState *states, int count);

int framed_data_tx_feed_state(FramedDataTxState *state,
                              const uint8_t *buf,
                              size_t len,
                              FramedDataTxPacketHandler on_tx,
                              void *tx_ctx,
                              FramedDataTxErrorHandler on_error,
                              void *error_ctx);

#endif
