#ifndef LORAHAM_DAEMON_RADIO_RUNTIME_H
#define LORAHAM_DAEMON_RADIO_RUNTIME_H

#include "daemon_led.h"
#include "daemon_tx_async_runtime.h"
#include "radio_controller.h"

/* --- Radio runtime helpers ---------------------------------------------- */

/* The one radio of this process (one process per band, see daemon_band.h). */
extern RadioController radio_controller;

void daemon_radio_controller_init(void);
void daemon_radio_shutdown_cleanup(void);
bool daemon_selected_radio_ready(void);
void daemon_log_active_radios(void);

/* RX-complete ISR callback targeting the single controller. */
void setFlag(void);

void daemon_radio_runtime_led(RadioController *ctrl,
                                            int state);

/*
 * Single source of truth for the per-band activity LED.
 *
 * The LED is ON whenever the band is transmitting or the channel/CAD is busy,
 * derived purely from the two atomics. Every runtime LED write goes through
 * here so the pin can never latch ON without a matching OFF.
 *
 * The TX worker thread and the main loop both reconcile the LED, so the
 * derive/cache-check/write sequence is held under led_mutex: this keeps the
 * cached led_state in lockstep with the physical pin and lets the cache safely
 * skip redundant GPIO writes. led_mutex is a leaf lock (only atomics and the
 * GPIO write happen inside it), so it introduces no lock-ordering risk.
 */
void daemon_radio_runtime_sync_led(RadioController *ctrl);

#endif
