#ifndef LORAHAM_DAEMON_TX_H
#define LORAHAM_DAEMON_TX_H

#include <stddef.h>
#include <stdint.h>

#include "tx_result.h"

/* --- Radio TX path ------------------------------------------------------- */

TxResult lora_send(uint8_t *buf, size_t len, int band);

#endif
