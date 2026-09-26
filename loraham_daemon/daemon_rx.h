#ifndef LORAHAM_DAEMON_RX_H
#define LORAHAM_DAEMON_RX_H

#include <stdint.h>

#include "daemon_protocol.h"

/* --- RX runtime helpers -------------------------------------------------- */

void daemon_process_radio(uint8_t (&rx_buf)[buf_SIZE]);

#endif
