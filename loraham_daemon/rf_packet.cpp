#include "rf_packet.h"

/* --- RF packet validation ------------------------------------------------ */

RfPacketValidation rf_packet_validate(const uint8_t *buf, size_t len)
{
    if (len == 0)
        return RF_PACKET_ERR_EMPTY;

    if (buf == 0)
        return RF_PACKET_ERR_NULL;

    if (len > RF_PACKET_MAX_PAYLOAD_LEN)
        return RF_PACKET_ERR_TOO_LONG;

    return RF_PACKET_VALID;
}

size_t rf_packet_preview_len(size_t len)
{
    if (len > RF_PACKET_PREVIEW_LEN)
        return RF_PACKET_PREVIEW_LEN;

    return len;
}

int rf_packet_lora_header_available(size_t len)
{
    return len >= RF_PACKET_LORA_HEADER_LEN ? 1 : 0;
}

const char *rf_packet_validation_message(RfPacketValidation state)
{
    switch (state) {
        case RF_PACKET_VALID:
            return "ok";
        case RF_PACKET_ERR_NULL:
            return "null buffer";
        case RF_PACKET_ERR_EMPTY:
            return "empty packet";
        case RF_PACKET_ERR_TOO_LONG:
            return "packet too long";
    }

    return "unknown packet error";
}
