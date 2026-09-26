#include "framed_data_tx.h"

#include <string.h>

/* --- Framed DATA TX stream state ---------------------------------------- */

void framed_data_tx_state_init(FramedDataTxState *state)
{
    if (!state)
        return;

    memset(state, 0, sizeof(*state));
}

void framed_data_tx_state_init_all(FramedDataTxState *states, int count)
{
    if (!states || count <= 0)
        return;

    for (int i = 0; i < count; i++)
        framed_data_tx_state_init(&states[i]);
}

static void framed_data_tx_error(FramedDataTxErrorHandler on_error,
                                 void *error_ctx,
                                 const char *msg)
{
    if (on_error)
        on_error(msg, error_ctx);
}

static int framed_data_tx_header_ready(FramedDataTxState *state,
                                       FramedDataTxErrorHandler on_error,
                                       void *error_ctx)
{
    uint8_t frame_type = state->header[0];
    uint16_t payload_len = (uint16_t)state->header[1] |
                           ((uint16_t)state->header[2] << 8);

    if (!framed_data_type_known(frame_type)) {
        framed_data_tx_error(on_error, error_ctx, "unknown frame type");
        framed_data_tx_state_init(state);
        state->skip_len = payload_len;
        return 0;
    }

    if (frame_type != FRAMED_DATA_TYPE_TX_PACKET) {
        if (payload_len > FRAMED_DATA_MAX_RF_PAYLOAD) {
            framed_data_tx_error(on_error, error_ctx, "unsupported frame too large");
            framed_data_tx_state_init(state);
            state->skip_len = payload_len;
            return 0;
        }

        state->frame_type = frame_type;
        state->expected_len = payload_len;
        return 0;
    }

    if (!framed_data_rf_payload_len_valid(payload_len)) {
        framed_data_tx_error(on_error, error_ctx, "TX payload too large");
        framed_data_tx_state_init(state);
        state->skip_len = payload_len;
        return 0;
    }

    state->frame_type = frame_type;
    state->expected_len = payload_len;

    return 0;
}

static int framed_data_tx_complete_frame(FramedDataTxState *state,
                                         FramedDataTxPacketHandler on_tx,
                                         void *tx_ctx,
                                         FramedDataTxErrorHandler on_error,
                                         void *error_ctx)
{
    int result = 0;

    if (state->frame_type == FRAMED_DATA_TYPE_TX_PACKET) {
        if (on_tx)
            result = on_tx(state->payload, state->expected_len, tx_ctx);
    } else {
        framed_data_tx_error(on_error, error_ctx, "unsupported client frame");
    }

    if (result != 0)
        framed_data_tx_error(on_error, error_ctx, "TX failed");

    framed_data_tx_state_init(state);

    return 0;
}

int framed_data_tx_feed_state(FramedDataTxState *state,
                              const uint8_t *buf,
                              size_t len,
                              FramedDataTxPacketHandler on_tx,
                              void *tx_ctx,
                              FramedDataTxErrorHandler on_error,
                              void *error_ctx)
{
    if (!state || (!buf && len > 0))
        return -1;

    for (size_t i = 0; i < len; i++) {
        /* Swallow the declared payload of a rejected frame (one error was
         * already reported); header parsing resumes right after it. */
        if (state->skip_len > 0) {
            state->skip_len--;
            continue;
        }

        if (state->header_len < FRAMED_DATA_HEADER_LEN) {
            state->header[state->header_len++] = buf[i];

            if (state->header_len == FRAMED_DATA_HEADER_LEN) {
                framed_data_tx_header_ready(state, on_error, error_ctx);

                if (state->header_len == 0)
                    continue;

                if (state->expected_len == 0 &&
                    framed_data_tx_complete_frame(state, on_tx, tx_ctx,
                                                  on_error, error_ctx) != 0)
                    return -1;
            }

            continue;
        }

        if (state->payload_len < state->expected_len) {
            state->payload[state->payload_len++] = buf[i];

            if (state->payload_len == state->expected_len &&
                framed_data_tx_complete_frame(state, on_tx, tx_ctx,
                                              on_error, error_ctx) != 0)
                return -1;
        }
    }

    return 0;
}
