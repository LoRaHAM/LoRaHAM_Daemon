#include "daemon_tx.h"
#include "daemon_rflog.h"
#include "radio_tx_limit.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <mutex>

#include <RadioLib.h>

#include "daemon_log.h"
#include "daemon_band.h"
#include "daemon_radio_runtime.h"
#include "daemon_rx_rearm.h"
#include "radio_controller.h"
#include "radio_health.h"
#include "rf_packet.h"

/* --- LoRa TX ------------------------------------------------------------- */
/* band stays a wire-adjacent validity check: only this process's band may
 * transmit through the single controller. */
static bool lora_send_valid_band(int band)
{
    return band == daemon_band()->band_number;
}

static const char *lora_tx_log_ctx(int band)
{
    (void)band;
    return daemon_band()->tx_log_ctx;
}

static bool lora_send_acquire_controller_tx(RadioController *ctrl)
{
    if (!ctrl)
        return false;

    if (ctrl->tx_busy.exchange(true))
        return false;

    ctrl->tx_status_generation.fetch_add(1u);
    return true;
}

static void lora_send_release_controller_tx(RadioController *ctrl)
{
    ctrl->tx_busy.store(false);
    ctrl->tx_status_generation.fetch_add(1u);
}

static void lora_print_tx_preview(const char *ctx,
                                  const uint8_t *buf,
                                  size_t len)
{
    char msg[512];
    size_t pos = 0;

    pos += snprintf(msg + pos, sizeof(msg) - pos, "%zu bytes: ", len);

    for(size_t i = 0; i < len && pos < sizeof(msg); i++)
        pos += snprintf(msg + pos, sizeof(msg) - pos, "%c",
                        buf[i] >= 32 && buf[i] <= 126 ? buf[i] : '.');

    printf("[%s] %s\n", ctx, msg);
    fflush(stdout);
}

static void lora_debug_tx_preview(const char *ctx,
                                  const uint8_t *buf,
                                  size_t len)
{
    size_t preview_len = rf_packet_preview_len(len);
    char msg[512];
    size_t pos = 0;

    pos += snprintf(msg + pos, sizeof(msg) - pos, "HEX:");

    for(size_t i = 0; i < preview_len && pos < sizeof(msg); i++)
        pos += snprintf(msg + pos, sizeof(msg) - pos, " %02X", buf[i]);

    if (preview_len < len && pos < sizeof(msg))
        snprintf(msg + pos, sizeof(msg) - pos, " ...");

    daemon_debug_ctx(ctx, "%s", msg);
}

static void lora_debug_tx_first_bytes(const char *ctx,
                                      const uint8_t *buf,
                                      size_t len)
{
    size_t preview_len = len < 7 ? len : 7;
    char msg[160];
    size_t pos = 0;

    pos += snprintf(msg + pos, sizeof(msg) - pos, "sending %zu bytes now", len);
    if (preview_len > 0) {
        pos += snprintf(msg + pos, sizeof(msg) - pos, " (");
        for (size_t i = 0; i < preview_len && pos < sizeof(msg); i++) {
            pos += snprintf(msg + pos, sizeof(msg) - pos,
                            "%s0x%02X", i > 0 ? " " : "", buf[i]);
        }
        snprintf(msg + pos, sizeof(msg) - pos, ")");
    }

    daemon_debug_ctx(ctx, "%s", msg);
}

static void lora_send_prepare_controller_tx(RadioController *ctrl)
{
    // TX ownership was acquired before payload copy/logging.

    // Clear RX state before switching the radio to TX.
    ctrl->received.store(false);

    // FSK uses a different receive path.
    if (ctrl->mode == RADIO_MODE_LORA)
        ctrl->driver->clearPacketReceivedAction();

    // Move the radio into a clean TX state.
    ctrl->driver->standby();
    ctrl->driver->clearIrq(0xFFFFFFFF);

    // Reset the TX FIFO only for 433/LoRa.
    // FSK sleep would reset the module configuration.
    if (ctrl->band == RADIO_BAND_433 && ctrl->mode == RADIO_MODE_LORA) {
        ctrl->driver->sleep();
        usleep(10000); // 10 ms
        ctrl->driver->standby();
        usleep(10000); // 10 ms
    }
}

static TxResult lora_send_controller(RadioController *ctrl,
                                     uint8_t *buf,
                                     size_t len)
{
    int band;
    const char *tag;
    const char *tx_ctx;

    if (!ctrl) {
        printf("[SEND 0] radio not ready: %s\n",
               radio_health_name(RADIO_HEALTH_FAILED));
        fflush(stdout);
        return TX_RESULT_RADIO_NOT_READY;
    }

    band = radio_controller_band_number(ctrl);
    tag = radio_controller_tag(ctrl);
    tx_ctx = lora_tx_log_ctx(band);

    if (!ctrl->driver || !radio_controller_ready(ctrl)) {
        printf("[SEND %d] radio not ready: %s\n",
               band, radio_health_name(radio_controller_health(ctrl)));
        fflush(stdout);
        return TX_RESULT_RADIO_NOT_READY;
    }

    RfPacketValidation packet_state = rf_packet_validate(buf, len);
    if (packet_state != RF_PACKET_VALID) {
        printf("[SEND %d] invalid TX packet: %s (%zu bytes)\n",
               band, rf_packet_validation_message(packet_state), len);
        fflush(stdout);
        return TX_RESULT_INVALID_PACKET;
    }

    /* Defence in depth at the last boundary before the chip. Callers already
     * chunk to this limit or reject against it; this is the one place no TX
     * can bypass, and it costs a comparison. */
    size_t payload_limit = radio_tx_payload_limit(ctrl);

    if (len > payload_limit) {
        printf("[SEND %d] invalid TX packet: %zu bytes exceed the %zu-byte "
               "limit in mode %s\n",
               band, len, payload_limit, radio_mode_name(ctrl->mode));
        fflush(stdout);
        return TX_RESULT_INVALID_PACKET;
    }

    if (!lora_send_acquire_controller_tx(ctrl)) {
        printf("[%s] TX BUSY - skipping\n", tag);
        fflush(stdout);
        return TX_RESULT_BUSY;
    }

    TxResult result;

    {
        std::lock_guard<std::recursive_mutex> radio_lock(ctrl->radio_mutex);

        // Copy the buffer after TX ownership and radio access are acquired.
        uint8_t send_buf[RF_PACKET_MAX_PAYLOAD_LEN];
        memcpy(send_buf, buf, len);

        lora_print_tx_preview(tx_ctx, send_buf, len);
        lora_debug_tx_preview(tx_ctx, send_buf, len);

        daemon_radio_runtime_sync_led(ctrl);
        lora_send_prepare_controller_tx(ctrl);

        if (ctrl->band == RADIO_BAND_433) {
            // Clear IRQs once more before TX.
            ctrl->driver->clearIrq(0xFFFFFFFF);

            daemon_debug_ctx(tx_ctx, "radio reconfigured");
            lora_debug_tx_first_bytes(tx_ctx, send_buf, len);
        }

        // transmit() blocks until the packet is sent.
        int state = ctrl->driver->transmit(send_buf, len);

        if(state != RADIOLIB_ERR_NONE) {
            daemon_debug_ctx(tx_ctx, "transmit error %d", state);
            /* One wording, and the tag from the controller: the two bands
             * printed "transmit ERROR" and "TX ERROR" for the same event,
             * which is a translation seam, not a distinction. */
            printf("[%s] transmit ERROR: %d\n", radio_controller_tag(ctrl), state);
        } else {
            daemon_debug_ctx(tx_ctx, "transmit OK");
            // Logged only now: a CAD refusal or a radio error never radiated.
            daemon_rflog_tx(radio_controller_tag(ctrl), "ok", send_buf, len);
        }

        if (ctrl->band == RADIO_BAND_868)
            usleep(50000); // 50 ms guard delay

        // Restore IRQ handling and RX callback after TX.
        // transmit() can clear the callback.
        ctrl->driver->clearIrq(0xFFFFFFFF);
        ctrl->received.store(false);
        ctrl->driver->setPacketReceivedAction(ctrl->rx_callback);
        daemon_rx_rearm_note_result(ctrl, ctrl->driver->startReceive(),
                                    "TX-Restore");

        result = state == RADIOLIB_ERR_NONE
            ? TX_RESULT_OK
            : TX_RESULT_RADIO_ERROR;
    }

    lora_send_release_controller_tx(ctrl);
    daemon_radio_runtime_sync_led(ctrl);
    return result;

}

TxResult lora_send(uint8_t *buf, size_t len, int band) {
    if (!lora_send_valid_band(band)) {
        printf("[SEND %d] invalid band\n", band);
        fflush(stdout);
        return TX_RESULT_INVALID_BAND;
    }

    if (!radio_controller_ready(&radio_controller)) {
        printf("[SEND %d] radio not ready: %s\n",
               band,
               radio_health_name(radio_controller_health(&radio_controller)));
        fflush(stdout);
        return TX_RESULT_RADIO_NOT_READY;
    }

    return lora_send_controller(&radio_controller, buf, len);
}
