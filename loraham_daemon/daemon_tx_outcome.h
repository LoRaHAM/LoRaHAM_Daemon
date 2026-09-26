#ifndef LORAHAM_DAEMON_TX_OUTCOME_H
#define LORAHAM_DAEMON_TX_OUTCOME_H

#include <stdint.h>

#include "framed_data.h"
#include "tx_result.h"

/* --- Interner TX-Ausgang ----------------------------------------------- */

typedef enum {
    DAEMON_TX_OUTCOME_OK = 0,
    DAEMON_TX_OUTCOME_BUSY,
    DAEMON_TX_OUTCOME_CHANNEL_BUSY,
    DAEMON_TX_OUTCOME_RADIO_NOT_READY,
    DAEMON_TX_OUTCOME_RADIO_ERROR,
    DAEMON_TX_OUTCOME_INVALID_PACKET,
    DAEMON_TX_OUTCOME_INVALID_BAND
} DaemonTxOutcome;

static inline int daemon_tx_outcome_is_ok(int outcome)
{
    return outcome == DAEMON_TX_OUTCOME_OK;
}

static inline int daemon_tx_outcome_is_failure(int outcome)
{
    return !daemon_tx_outcome_is_ok(outcome);
}

static inline DaemonTxOutcome daemon_tx_outcome_from_tx_result(TxResult result)
{
    switch (result) {
        case TX_RESULT_OK:
            return DAEMON_TX_OUTCOME_OK;
        case TX_RESULT_BUSY:
            return DAEMON_TX_OUTCOME_BUSY;
        case TX_RESULT_CAD_TIMEOUT:
            return DAEMON_TX_OUTCOME_CHANNEL_BUSY;
        case TX_RESULT_RADIO_NOT_READY:
            return DAEMON_TX_OUTCOME_RADIO_NOT_READY;
        case TX_RESULT_INVALID_PACKET:
            return DAEMON_TX_OUTCOME_INVALID_PACKET;
        case TX_RESULT_INVALID_BAND:
            return DAEMON_TX_OUTCOME_INVALID_BAND;
        case TX_RESULT_RADIO_ERROR:
        default:
            return DAEMON_TX_OUTCOME_RADIO_ERROR;
    }
}

static inline TxResult daemon_tx_outcome_to_tx_result(DaemonTxOutcome outcome)
{
    switch (outcome) {
        case DAEMON_TX_OUTCOME_OK:
            return TX_RESULT_OK;
        case DAEMON_TX_OUTCOME_BUSY:
            return TX_RESULT_BUSY;
        case DAEMON_TX_OUTCOME_CHANNEL_BUSY:
            return TX_RESULT_CAD_TIMEOUT;
        case DAEMON_TX_OUTCOME_RADIO_NOT_READY:
            return TX_RESULT_RADIO_NOT_READY;
        case DAEMON_TX_OUTCOME_INVALID_PACKET:
            return TX_RESULT_INVALID_PACKET;
        case DAEMON_TX_OUTCOME_INVALID_BAND:
            return TX_RESULT_INVALID_BAND;
        case DAEMON_TX_OUTCOME_RADIO_ERROR:
        default:
            return TX_RESULT_RADIO_ERROR;
    }
}

static inline uint8_t daemon_tx_outcome_to_framed_status(int outcome)
{
    switch (outcome) {
        case DAEMON_TX_OUTCOME_OK:
            return FRAMED_DATA_TX_STATUS_OK;
        case DAEMON_TX_OUTCOME_BUSY:
            return FRAMED_DATA_TX_STATUS_BUSY;
        case DAEMON_TX_OUTCOME_CHANNEL_BUSY:
            return FRAMED_DATA_TX_STATUS_CHANNEL_BUSY;
        case DAEMON_TX_OUTCOME_RADIO_NOT_READY:
            return FRAMED_DATA_TX_STATUS_RADIO_NOT_READY;
        case DAEMON_TX_OUTCOME_INVALID_PACKET:
            return FRAMED_DATA_TX_STATUS_INVALID_PACKET;
        case DAEMON_TX_OUTCOME_INVALID_BAND:
            return FRAMED_DATA_TX_STATUS_INVALID_BAND;
        case DAEMON_TX_OUTCOME_RADIO_ERROR:
        default:
            return FRAMED_DATA_TX_STATUS_RADIO_ERROR;
    }
}

#endif
