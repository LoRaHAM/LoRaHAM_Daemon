#include "radio_tx_limit.h"

#include "radio_controller.h"
#include "radio_driver.h"
#include "rf_packet.h"

/* SX127x FSK: 64-byte FIFO, and in variable-length mode the length byte goes
 * into it, so 63 payload bytes are what actually fit. */
#define RADIO_TX_FSK_SX127X_MAX_PAYLOAD 63

size_t radio_tx_payload_limit(DaemonChipFamily family, RadioMode_t mode)
{
    if (family == DAEMON_CHIP_FAMILY_SX127X && mode == RADIO_MODE_FSK)
        return RADIO_TX_FSK_SX127X_MAX_PAYLOAD;

    return RF_PACKET_MAX_PAYLOAD_LEN;
}

size_t radio_tx_payload_limit(const RadioController *ctrl)
{
    if (!ctrl || !ctrl->driver)
        return RF_PACKET_MAX_PAYLOAD_LEN;

    return radio_tx_payload_limit(ctrl->driver->chipFamily(), ctrl->mode);
}
