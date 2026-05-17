#include "tx_result.h"

/* --- TX result helpers --------------------------------------------------- */

const char *tx_result_name(TxResult result)
{
    switch (result) {
        case TX_RESULT_OK:
            return "OK";
        case TX_RESULT_INVALID_BAND:
            return "INVALID_BAND";
        case TX_RESULT_INVALID_PACKET:
            return "INVALID_PACKET";
        case TX_RESULT_BUSY:
            return "BUSY";
        case TX_RESULT_CAD_TIMEOUT:
            return "CAD_TIMEOUT";
        case TX_RESULT_RADIO_ERROR:
            return "RADIO_ERROR";
    }

    return "UNKNOWN";
}

int tx_result_is_ok(TxResult result)
{
    return result == TX_RESULT_OK;
}
