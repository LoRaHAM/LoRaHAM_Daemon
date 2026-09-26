#ifndef LORAHAM_DAEMON_BAND_H
#define LORAHAM_DAEMON_BAND_H

#include "radio_channel.h"   /* RadioBand_t */

struct RadioRfDefaults;

/* --- Band descriptor ------------------------------------------------------ */
/*
 * One process drives exactly one band. The descriptor bundles every per-band
 * constant (frozen socket paths, log tags, RF boot defaults, legacy LED pin)
 * so runtime code is written once and reads its band context from here
 * instead of duplicating 433/868 paths.
 *
 * Resolution happens exactly once, at the end of argument parsing, from the
 * mandatory --radio selection; the descriptor is immutable afterwards.
 */

typedef struct {
    RadioBand_t band;              /* RADIO_BAND_433 / RADIO_BAND_868 */
    int         band_number;       /* 433 / 868 */
    const char *tag;               /* "433" / "868" */
    bool        is_hf;
    const char *data_socket;       /* frozen paths from daemon_protocol.h */
    const char *framed_socket;
    const char *conf_socket;
    const char *tx_log_ctx;        /* "TX433" / "TX868" */
    const char *framed_log_ctx;    /* "TX433F" / "TX868F" */
    const char *rx_log_ctx;        /* "RX433" / "RX868" */
    const char *conf_log_ctx;      /* "CONF433" / "CONF868" */
    const char *config_log_ctx;    /* "CONFIG433" / "CONFIG868" */
    const char *client_log_ctx;    /* "CLIENT433" / "CLIENT868" */
    const char *cad_log_ctx;       /* "CAD433" / "CAD868" */
    const char *rssi_log_ctx;      /* "RSSI433" / "RSSI868" */
    /* Operational frequency policy: SET FREQ outside this range
     * is rejected so an --radio 868 process can never tune off its logical
     * band (locks/sockets/logs would keep lying about the band). */
    float       freq_min_mhz;
    float       freq_max_mhz;
    const struct RadioRfDefaults *rf_defaults; /* boot RF defaults */
    int         legacy_led_pin;    /* DAEMON_LED_PIN_433 / _868 */
} DaemonBandDescriptor;

/* Resolve from the --radio selection (RADIO_BAND_433 / RADIO_BAND_868).
 * Called once at the end of daemon_parse_args; also used by unit tests. */
void daemon_band_resolve(RadioBand_t band);

/* The resolved band descriptor. Never NULL after daemon_band_resolve();
 * fails closed (abort) when read before resolution. */
const DaemonBandDescriptor *daemon_band(void);

#endif
