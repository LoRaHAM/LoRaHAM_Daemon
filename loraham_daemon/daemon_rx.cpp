#include "daemon_rx.h"
#include "daemon_rflog.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <mutex>

#include <RadioLib.h>

#include "client_slot.h"
#include "daemon_band.h"
#include "daemon_log.h"
#include "daemon_protocol.h"
#include "daemon_radio_runtime.h"
#include "daemon_rx_rearm.h"
#include "daemon_stats.h"
#include "framed_data.h"
#include "radio_channel.h"
#include "radio_controller.h"
#include "rf_packet.h"

/* --- External daemon channel state -------------------------------------- */

extern RadioChannelIo channel;

/* --- RX drop logging ----------------------------------------------------- */
// Rate-limited counters for invalid RadioLib RX reads.
#define RX_DROP_LOG_INITIAL 5
#define RX_DROP_LOG_INTERVAL 100


/* --- RX log context ------------------------------------------------------ */
static const char *daemon_rx_log_ctx(RadioController *ctrl)
{
    (void)ctrl;
    return daemon_band()->rx_log_ctx;
}

static void daemon_note_rx_flag_observed(RadioController *ctrl)
{
    daemon_debug_ctx(daemon_rx_log_ctx(ctrl), "flag set");
}

/* --- RX special cases ---------------------------------------------------- */
static void daemon_discard_rx_during_tx(RadioController *ctrl)
{
    ctrl->received.store(false);
    daemon_debug_ctx(daemon_rx_log_ctx(ctrl), "RX during TX discarded");
    printf("[%s] RX during TX - discarding packet\n",
           radio_controller_tag(ctrl));
}

/* --- RX output ----------------------------------------------------------- */
static void daemon_debug_hex_bytes(const char *ctx, const uint8_t *buf, int len)
{
    char msg[512];
    size_t pos = 0;

    pos += snprintf(msg + pos, sizeof(msg) - pos, "HEX:");
    for (int i = 0; i < len; i++) {
        if (pos + 4 >= sizeof(msg)) {
            snprintf(msg + pos, sizeof(msg) - pos, " ...");
            break;
        }

        pos += snprintf(msg + pos, sizeof(msg) - pos, " %02X", buf[i]);
    }

    daemon_debug_ctx(ctx, "%s", msg);
}

static void daemon_print_ascii_bytes(const uint8_t *buf, int len)
{
    for (int i = 0; i < len; i++) {
        if (buf[i] >= 32 && buf[i] <= 126)
            printf("%c", buf[i]);
        else
            printf(".");
    }
}

/* --- RX presentation metadata ------------------------------------------- */
static const char *daemon_controller_color(RadioController *ctrl)
{
    if (ctrl->band == RADIO_BAND_433)
        return "93m";

    return "32m";
}

static void daemon_print_raw_rx_packet(const char *rx_ctx,
                                       const char *band,
                                       const char *color,
                                       const char *suffix,
                                       uint8_t *buf,
                                       int len,
                                       float rssi)
{
    daemon_debug_hex_bytes(rx_ctx, buf, len);

    printf("[\033[%s%s%s\033[0m] %d bytes ASCII: ",
           color, band, suffix ? suffix : "", len);
    daemon_print_ascii_bytes(buf, len);
    printf(" RSSI: %.2f dBm\n", rssi);
}

static void daemon_print_lora_packet(const char *rx_ctx,
                                     const char *band,
                                     const char *color,
                                     uint8_t *buf,
                                     int len,
                                     float rssi)
{
    if (!rf_packet_lora_header_available((size_t)len)) {
        daemon_debug_ctx(rx_ctx,
                         "LoRa short packet %d bytes, header skipped",
                         len);
        daemon_print_raw_rx_packet(rx_ctx, band, color, "-SHORT",
                                   buf, len, rssi);
        return;
    }

    /* Cast before shifting: uint8_t promotes to signed int, and a set top
     * bit shifted by 24 is signed-overflow UB. */
    uint32_t toNode      = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
                           ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
    uint32_t fromNode    = (uint32_t)buf[4] | ((uint32_t)buf[5] << 8) |
                           ((uint32_t)buf[6] << 16) | ((uint32_t)buf[7] << 24);
    uint32_t uniqueID    = (uint32_t)buf[8] | ((uint32_t)buf[9] << 8) |
                           ((uint32_t)buf[10] << 16) | ((uint32_t)buf[11] << 24);

    uint8_t hdrFlags     = buf[12];
    uint8_t chHash       = buf[13];
    uint8_t nextHop      = buf[14];
    uint8_t rlyNodes     = buf[15];

    daemon_debug_ctx(rx_ctx,
                     "LoRa %d bytes from %08X to %08X ID:%08X Flag:%02X Hash:%02X Hop:%02X Node:%02X RSSI: %.2f dBm",
                     len, fromNode, toNode, uniqueID,
                     hdrFlags, chHash, nextHop, rlyNodes, rssi);
    daemon_debug_hex_bytes(rx_ctx, buf, len);

    printf("[\033[%s%s\033[0m] %d bytes ASCII: ", color, band, len);
    daemon_print_ascii_bytes(buf, len);
    printf(" RSSI: %.2f dBm\n", rssi);
}

static void daemon_print_fsk_packet(const char *rx_ctx,
                                    const char *band,
                                    const char *color,
                                    uint8_t *buf,
                                    int len,
                                    float rssi)
{
    daemon_debug_hex_bytes(rx_ctx, buf, len);

    printf("[\033[%s%s-FSK\033[0m] %d bytes ASCII: ", color, band, len);
    daemon_print_ascii_bytes(buf, len);
    printf(" RSSI: %.2f dBm\n", rssi);
}


/* --- RX signal metadata -------------------------------------------------- */
typedef struct {
    float rssi_dbm;
    int16_t rssi_cdbm;
    int16_t snr_cdb;
} DaemonRxSignal;

static int16_t daemon_signal_to_centi(float value)
{
    double scaled;
    long rounded;

    if (!(value >= -10000.0f && value <= 10000.0f))
        return FRAMED_DATA_SIGNAL_UNAVAILABLE;

    scaled = (double)value * 100.0;
    if (scaled >= 0.0)
        rounded = (long)(scaled + 0.5);
    else
        rounded = (long)(scaled - 0.5);

    if (rounded > 32767L)
        return 32767;
    if (rounded < -32768L)
        return FRAMED_DATA_SIGNAL_UNAVAILABLE;

    return (int16_t)rounded;
}

static DaemonRxSignal daemon_capture_rx_signal(RadioController *ctrl)
{
    DaemonRxSignal signal;

    signal.rssi_dbm = -200.0f;
    signal.rssi_cdbm = FRAMED_DATA_SIGNAL_UNAVAILABLE;
    signal.snr_cdb = FRAMED_DATA_SIGNAL_UNAVAILABLE;

    if (!ctrl || !ctrl->driver)
        return signal;

    signal.rssi_dbm = ctrl->driver->getRSSI();
    signal.rssi_cdbm = daemon_signal_to_centi(signal.rssi_dbm);

    if (ctrl->mode == RADIO_MODE_LORA)
        signal.snr_cdb = daemon_signal_to_centi(ctrl->driver->getSNR());

    return signal;
}

/* --- RX forwarding ------------------------------------------------------- */
static void daemon_broadcast_rx_data(RadioChannelIo *io,
                                     uint8_t *buf,
                                     int len,
                                     int16_t rssi_cdbm,
                                     int16_t snr_cdb)
{
    uint8_t frame[FRAMED_DATA_RX_FRAME_MAX];
    uint16_t payload_len;

    if (len <= 0)
        return;

    client_slot_broadcast_bytes_queued(io->data_slots, MAX_CLIENTS, buf, len);

    if ((size_t)len > FRAMED_DATA_MAX_RF_PAYLOAD) {
        daemon_debug_ctx("RXF", "RX frame too large: %d", len);
        return;
    }

    if (framed_data_encode_rx_packet(frame,
                                     sizeof(frame),
                                     rssi_cdbm,
                                     snr_cdb,
                                     buf,
                                     (uint16_t)len) != 0) {
        daemon_debug_ctx("RXF", "RX frame encode failed");
        return;
    }

    payload_len = (uint16_t)(FRAMED_DATA_RX_META_LEN + len);
    client_slot_broadcast_bytes_queued(io->framed_data_slots,
                                       MAX_CLIENTS,
                                       frame,
                                       framed_data_frame_size(payload_len));
}

/* --- RX IRQ/FIFO sequence ------------------------------------------------ */
static void daemon_restart_receive_after_empty_rx(RadioController *ctrl)
{
    daemon_debug_ctx(daemon_rx_log_ctx(ctrl), "empty IRQ, restarting RX");
    ctrl->driver->clearIrq(0xFFFFFFFF);
    daemon_rx_rearm_note_result(ctrl, ctrl->driver->startReceive(),
                                "RX empty IRQ");
}

static void daemon_finish_rx_packet(RadioController *ctrl,
                                    uint8_t *buf,
                                    size_t buf_len)
{
    if (ctrl->band == RADIO_BAND_868)
        memset(buf, 0, buf_len);

    // Symmetric LoRa IRQ clearing: clear RxDone *after* readData() and before
    // re-arming, so the just-read packet cannot be re-delivered on the next
    // startReceive (standard SX127x order: readData -> clearIrq -> startReceive).
    // FSK clears in daemon_clear_irq_after_rx_read and stays unchanged.
    if (ctrl->mode == RADIO_MODE_LORA)
        ctrl->driver->clearIrq(0xFFFFFFFF);

    daemon_rx_rearm_note_result(ctrl, ctrl->driver->startReceive(),
                                "RX restart");
    daemon_debug_ctx(daemon_rx_log_ctx(ctrl), "RX ready");
}

static void daemon_prepare_rx_packet(RadioController *ctrl,
                                     uint8_t *buf,
                                     size_t buf_len)
{
    ctrl->received.store(false);
    memset(buf, 0, buf_len);

    // Do NOT clear the IRQ flags before readData() — in either mode.
    //
    // LoRa: RadioLib derives the packet verdict from the IRQ flags *at read
    // time* — SX127x::readData() reads PayloadCrcError (and the header's
    // CRC-on-payload bit), SX126x::readData() reads CRC_ERR / HEADER_ERR —
    // and returns RADIOLIB_ERR_CRC_MISMATCH / RADIOLIB_ERR_LORA_HEADER_DAMAGED
    // so that daemon_rx_read_ok() drops the frame. Clearing the flags here
    // wiped that evidence first: every corrupted frame read as ERR_NONE and was
    // handed to the clients as valid (seen on the air 2026-09-13: a chat frame
    // at -120.75 dBm / SNR -6.75 delivered with bit errors in callsign and
    // text although CRC=1 on both sides). readData() clears the flags itself
    // after the read; daemon_finish_rx_packet() clears again before re-arming.
    //
    // FSK: never clear before reading the FIFO — writing IRQ flags here can set
    // FifoOverrun and empty the FIFO (unchanged).
}

static int daemon_rx_packet_length(RadioController *ctrl)
{
    int len = ctrl->driver->getPacketLength();

    daemon_debug_ctx(daemon_rx_log_ctx(ctrl), "length %d", len);
    return len;
}

static void daemon_clear_irq_after_rx_read(RadioController *ctrl)
{
    if (ctrl->mode == RADIO_MODE_FSK) {
        daemon_debug_ctx(daemon_rx_log_ctx(ctrl), "clearing IRQ after read");
        ctrl->driver->clearIrq(0xFFFFFFFF);
    }
}

static void daemon_print_rx_packet(RadioController *ctrl,
                                   uint8_t *buf,
                                   int len,
                                   float rssi)
{
    // Console/log RX dump is debug-only: in normal operation it would grow the
    // -d logfile unbounded with every received packet. RX forwarding to the
    // raw/framed sockets is independent and stays unconditional.
    if (!daemon_debug_enabled())
        return;

    const char *tag = radio_controller_tag(ctrl);
    const char *color = daemon_controller_color(ctrl);

    if (ctrl->mode == RADIO_MODE_LORA)
        daemon_print_lora_packet(daemon_rx_log_ctx(ctrl), tag, color, buf, len, rssi);
    else
        daemon_print_fsk_packet(daemon_rx_log_ctx(ctrl), tag, color, buf, len, rssi);
}

/* --- RX radio accessors -------------------------------------------------- */
static int16_t daemon_read_rx_data(RadioController *ctrl,
                                   uint8_t *buf,
                                   size_t buf_len)
{
    return ctrl->driver->readData(buf, buf_len);
}

/* --- RX read validation -------------------------------------------------- */
static bool daemon_should_log_rx_drop(unsigned long drops)
{
    return drops <= RX_DROP_LOG_INITIAL ||
           (RX_DROP_LOG_INTERVAL > 0 && drops % RX_DROP_LOG_INTERVAL == 0);
}

static void daemon_record_rx_drop(RadioController *ctrl, int16_t state)
{
    daemon_radio_stats_record_rx_drop(&ctrl->stats);
    daemon_debug_ctx(daemon_rx_log_ctx(ctrl), "drop %lu status %d",
                     ctrl->stats.rx_drops, state);

    if (daemon_should_log_rx_drop(ctrl->stats.rx_drops)) {
        printf("[%d] RX read error: %d, packet dropped, drops=%lu\n",
               radio_controller_band_number(ctrl), state, ctrl->stats.rx_drops);
        fflush(stdout);
    }
}

static bool daemon_rx_read_ok(RadioController *ctrl, int16_t state)
{
    if (state == RADIOLIB_ERR_NONE)
        return true;

    daemon_record_rx_drop(ctrl, state);
    return false;
}

static void daemon_record_rx_invalid_packet(RadioController *ctrl,
                                            const char *reason,
                                            int len)
{
    daemon_radio_stats_record_rx_drop(&ctrl->stats);
    daemon_debug_ctx(daemon_rx_log_ctx(ctrl),
                     "Drop %lu invalid packet: %s (%d bytes)",
                     ctrl->stats.rx_drops,
                     reason ? reason : "invalid",
                     len);

    if (daemon_should_log_rx_drop(ctrl->stats.rx_drops)) {
        printf("[%d] RX invalid packet: %s (%d bytes), packet dropped, drops=%lu\n",
               radio_controller_band_number(ctrl),
               reason ? reason : "invalid",
               len,
               ctrl->stats.rx_drops);
        fflush(stdout);
    }
}

static bool daemon_rx_length_ok(RadioController *ctrl,
                                int len,
                                size_t buf_len)
{
    if (len <= 0)
        return false;

    if ((size_t)len > buf_len || (size_t)len > RF_PACKET_MAX_PAYLOAD_LEN) {
        daemon_record_rx_invalid_packet(ctrl, "packet too long", len);
        return false;
    }

    return true;
}

static bool daemon_rx_packet_ok(RadioController *ctrl,
                                uint8_t *buf,
                                int len)
{
    RfPacketValidation packet_state =
        rf_packet_validate(buf, (size_t)len);

    if (packet_state != RF_PACKET_VALID) {
        daemon_record_rx_invalid_packet(
            ctrl,
            rf_packet_validation_message(packet_state),
            len);
        return false;
    }

    return true;
}

static void daemon_drop_invalid_rx_packet(RadioController *ctrl)
{
    ctrl->received.store(false);
    ctrl->driver->clearIrq(0xFFFFFFFF);
    daemon_rx_rearm_note_result(ctrl, ctrl->driver->startReceive(),
                                "RX-Drop");
    daemon_debug_ctx(daemon_rx_log_ctx(ctrl), "RX ready after drop");
}


/* --- RX band flow -------------------------------------------------------- */
static void daemon_process_radio_band(RadioController *ctrl,
                                      RadioChannelIo *io,
                                      uint8_t (&rx_buf)[buf_SIZE])
{
    if (!radio_controller_ready(ctrl) || !ctrl->driver)
        return;

    // Nothing pending?
    if (!ctrl->received.load())
        return;

    daemon_note_rx_flag_observed(ctrl);

    /* Lock discipline (see radio_controller.h): never block the main loop on
     * radio_mutex while a TX can be in flight — the worker holds the mutex
     * across the blocking transmit() (seconds at SF12). Pre-check tx_busy
     * BEFORE taking the lock; the under-lock re-check below stays for the
     * race where TX starts between check and acquisition. */
    if (ctrl->tx_busy.load()) {
        daemon_discard_rx_during_tx(ctrl);
        return;
    }

    std::lock_guard<std::recursive_mutex> radio_lock(ctrl->radio_mutex);

    if (ctrl->tx_busy.load()) {
        daemon_discard_rx_during_tx(ctrl);
        return;
    }

    daemon_prepare_rx_packet(ctrl, rx_buf, sizeof(rx_buf));

    int len = daemon_rx_packet_length(ctrl);
    // Spurious IRQ: no packet is available.
    if (len <= 0) {
        // FSK IRQ clear is safe here because the FIFO is empty.
        daemon_restart_receive_after_empty_rx(ctrl);
        return;
    }

    if (!daemon_rx_length_ok(ctrl, len, sizeof(rx_buf))) {
        daemon_drop_invalid_rx_packet(ctrl);
        return;
    }

    daemon_debug_ctx(daemon_rx_log_ctx(ctrl), "readData()");
    int16_t read_state = daemon_read_rx_data(ctrl, rx_buf, sizeof(rx_buf)); // 5 ms timeout

    // In FSK mode, clear IRQs after readData() emptied the FIFO.
    daemon_clear_irq_after_rx_read(ctrl);

    if (!daemon_rx_read_ok(ctrl, read_state)) {
        daemon_finish_rx_packet(ctrl, rx_buf, sizeof(rx_buf));
        return;
    }

    daemon_debug_ctx(daemon_rx_log_ctx(ctrl), "read OK");

    if (!daemon_rx_packet_ok(ctrl, rx_buf, len)) {
        daemon_finish_rx_packet(ctrl, rx_buf, sizeof(rx_buf));
        return;
    }

    DaemonRxSignal signal = daemon_capture_rx_signal(ctrl);

    daemon_radio_stats_record_rx(&ctrl->stats, (size_t)len);
    daemon_print_rx_packet(ctrl, rx_buf, len, signal.rssi_dbm);
    // The RF log records what the radio delivered, with the signal data in hand,
    // right where the frame is handed to every client.
    daemon_rflog_rx(radio_controller_tag(ctrl), signal.rssi_cdbm, signal.snr_cdb,
                    rx_buf, (size_t)len);
    daemon_broadcast_rx_data(io,
                             rx_buf,
                             len,
                             signal.rssi_cdbm,
                             signal.snr_cdb);
    daemon_debug_ctx(daemon_rx_log_ctx(ctrl), "broadcast %d bytes", len);

    daemon_finish_rx_packet(ctrl, rx_buf, sizeof(rx_buf));
}


/* --- RX entry point ------------------------------------------------------- */
void daemon_process_radio(uint8_t (&rx_buf)[buf_SIZE])
{
    daemon_rx_rearm_retry(&radio_controller);
    daemon_process_radio_band(&radio_controller, &channel, rx_buf);
}
