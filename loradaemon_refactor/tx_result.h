#ifndef LORAHAM_TX_RESULT_H
#define LORAHAM_TX_RESULT_H

/* --- TX result propagation --- */

typedef enum {
    TX_RESULT_OK = 0,
    TX_RESULT_INVALID_BAND,
    TX_RESULT_INVALID_PACKET,
    TX_RESULT_BUSY,
    TX_RESULT_CAD_TIMEOUT,
    TX_RESULT_RADIO_ERROR
} TxResult;

const char *tx_result_name(TxResult result);
int tx_result_is_ok(TxResult result);

#endif
