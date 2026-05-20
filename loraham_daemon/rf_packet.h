#ifndef LORAHAM_RF_PACKET_H
#define LORAHAM_RF_PACKET_H

#include <stddef.h>
#include <stdint.h>

/* --- RF packet limits ---------------------------------------------------- */

#define RF_PACKET_MAX_PAYLOAD_LEN 255
#define RF_PACKET_PREVIEW_LEN 20

typedef enum {
    RF_PACKET_VALID = 0,
    RF_PACKET_ERR_NULL = -1,
    RF_PACKET_ERR_EMPTY = -2,
    RF_PACKET_ERR_TOO_LONG = -3
} RfPacketValidation;

RfPacketValidation rf_packet_validate(const uint8_t *buf, size_t len);
size_t rf_packet_preview_len(size_t len);
const char *rf_packet_validation_message(RfPacketValidation state);

#endif
