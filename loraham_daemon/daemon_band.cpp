#include "daemon_band.h"

#include <stdio.h>
#include <stdlib.h>

#include "daemon_led.h"
#include "daemon_protocol.h"
#include "radio_rf_defaults.h"

/* --- Boot-RF-Defaults ------------------------------------------------------ */
// Per-band boot defaults, applied inside RadioDriver::begin() in the exact
// pre-driver setter order. LDRO: 433 = autoLDRO()+forceLDRO(1), 868 = only
// autoLDRO() (Feld ldro: >=0 forciert, <0 auto).

// LoRa-APRS:
static const RadioRfDefaults rf_defaults_433 = {
    433.900f,   /* freq_mhz */
    12,         /* spreading_factor */
    125.0f,     /* bandwidth_khz */
    0x12,       /* sync_word */
    8,          /* preamble_len */
    5,          /* coding_rate */
    true,       /* crc_on */
    1,          /* ldro: autoLDRO() + forceLDRO(1) */
    10          /* power_dbm */
};

/*
*
* // LoRa DX Cluster:
*    433.900 / SF10 / BW125.0 / Sync 0x12 / Preamble 8 / CR5 / CRC an /
*    autoLDRO() / 10 dBm
*
*
*
* // Meshtastic 433:
*
*    433.900 (DB0ARD) / SF11 / BW125.0 / Sync 0x2B / Preamble 16 / CR5 /
*    CRC an / autoLDRO() / 10 dBm
*
*
* // Meshcom:
*
*    433.175 / SF11 / BW250.0 / Sync 0x2B / Preamble 8 / CR6 / CRC an /
*    (kein autoLDRO) / 10 dBm
*/

static const RadioRfDefaults rf_defaults_868 = {
    869.525f,   /* freq_mhz */
    11,         /* spreading_factor */
    250.0f,     /* bandwidth_khz */
    0x2B,       /* sync_word */
    16,         /* preamble_len */
    5,          /* coding_rate */
    true,       /* crc_on */
    -1,         /* ldro: autoLDRO() only */
    10          /* power_dbm */
};

/* --- Band descriptors ------------------------------------------------------ */

static const DaemonBandDescriptor band_433 = {
    RADIO_BAND_433,
    433,
    "433",
    false,
    DATA433_SOCKET,
    DATA433_FRAMED_SOCKET,
    CONF433_SOCKET,
    "TX433",
    "TX433F",
    "RX433",
    "CONF433",
    "CONFIG433",
    "CLIENT433",
    "CAD433",
    "RSSI433",
    430.0f,
    440.0f,
    &rf_defaults_433,
    DAEMON_LED_PIN_433
};

static const DaemonBandDescriptor band_868 = {
    RADIO_BAND_868,
    868,
    "868",
    true,
    DATA868_SOCKET,
    DATA868_FRAMED_SOCKET,
    CONF868_SOCKET,
    "TX868",
    "TX868F",
    "RX868",
    "CONF868",
    "CONFIG868",
    "CLIENT868",
    "CAD868",
    "RSSI868",
    863.0f,
    870.0f,
    &rf_defaults_868,
    DAEMON_LED_PIN_868
};

static const DaemonBandDescriptor *g_band = NULL;

/* LORAHAM_SOCKET_DIR override (dev/test only; audit item 3): resolved once,
 * here, into static buffers — every consumer keeps reading the immutable
 * descriptor. Empty/unset env means the production /run/loraham defaults. */
static DaemonBandDescriptor g_band_override;
static char g_sock_data[192];
static char g_sock_framed[192];
static char g_sock_conf[192];

void daemon_band_resolve(RadioBand_t band)
{
    const DaemonBandDescriptor *table =
        (band == RADIO_BAND_868) ? &band_868 : &band_433;
    const char *dir = getenv("LORAHAM_SOCKET_DIR");

    if (!dir || dir[0] == '\0') {
        g_band = table;
        return;
    }

    g_band_override = *table;
    snprintf(g_sock_data, sizeof(g_sock_data), "%s/%s", dir,
             band == RADIO_BAND_868 ? DATA868_SOCKET_NAME
                                    : DATA433_SOCKET_NAME);
    snprintf(g_sock_framed, sizeof(g_sock_framed), "%s/%s", dir,
             band == RADIO_BAND_868 ? DATA868_FRAMED_SOCKET_NAME
                                    : DATA433_FRAMED_SOCKET_NAME);
    snprintf(g_sock_conf, sizeof(g_sock_conf), "%s/%s", dir,
             band == RADIO_BAND_868 ? CONF868_SOCKET_NAME
                                    : CONF433_SOCKET_NAME);
    g_band_override.data_socket = g_sock_data;
    g_band_override.framed_socket = g_sock_framed;
    g_band_override.conf_socket = g_sock_conf;
    g_band = &g_band_override;
}

const DaemonBandDescriptor *daemon_band(void)
{
    if (!g_band) {
        /* Fail closed: reading the band before --radio resolution is a
         * programming error, never a recoverable runtime state. */
        fprintf(stderr, "[Daemon] daemon_band() read before resolution\n");
        abort();
    }

    return g_band;
}
