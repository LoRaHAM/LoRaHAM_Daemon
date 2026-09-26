#ifndef LORAHAM_RADIO_CONTROLLER_H
#define LORAHAM_RADIO_CONTROLLER_H

#include <memory>
#include <atomic>
#include <mutex>

#include "hal/RPi/PiHal.h"
#include <RadioLib.h>

#include "daemon_tx_policy.h"
#include "radio_channel.h"
#include "radio_driver.h"
#include "daemon_stats.h"
#include "radio_health.h"
#include "hardware_profile.h"   /* DaemonChipFamily, for the STATUS witness */

/* --- Radio hardware/runtime state --------------------------------------- */

// Default RSSI threshold (dBm) above which the passive monitoring probe reports
// the channel busy. Environment dependent and tunable per band at runtime
// (SET CADRSSI) or at boot (--cad-rssi). Used for the CAD=0/1 monitoring
// indicator AND — on wiring without DIO1 (cad_scan_available=false, e.g. the
// Uputronics boards) — as the passive-LBT fallback threshold that gates
// MANAGED TX. On such hardware, changing CADRSSI changes whether TX proceeds.
#define RADIO_CAD_RSSI_BUSY_THRESHOLD_DBM (-90.0f)

typedef enum {
    RADIO_TX_MODE_MANAGED = 0,
    RADIO_TX_MODE_DIRECT = 1
} RadioTxMode_t;

static inline const char *radio_tx_mode_name(RadioTxMode_t mode)
{
    return mode == RADIO_TX_MODE_DIRECT ? "DIRECT" : "MANAGED";
}

static inline const char *radio_mode_name(RadioMode_t mode)
{
    return mode == RADIO_MODE_FSK ? "FSK" : "LORA";
}

// Chip access runs through the RadioDriver runtime interface; the controller
// itself is chip-agnostic (no template parameter anymore).
struct RadioController {
    RadioBand_t band;
    const char *tag;
    bool is_hf;

    std::unique_ptr<PiHal> hal;
    std::unique_ptr<Module> mod;
    std::unique_ptr<RadioDriver> driver;
    /*
     * LOCK-ORDERING / LOCK-DISCIPLINE CONTRACT
     *
     * Order: radio_mutex first, then the process-shared SPI flock (taken only
     * inside a single SPI transaction, lowest and last — see locking_pihal.h).
     * led_mutex is a leaf lock (only atomics and the GPIO write inside it) and
     * never nests with either.
     *
     * Access rule: the TX WORKER may hold radio_mutex across the blocking
     * transmit() (seconds at SF12). Therefore NO MAIN-LOOP path may block on
     * radio_mutex while a TX can be in flight: main-loop paths gate on
     * tx_busy and/or acquire with try_to_lock and skip the tick on contention
     * (RX poll, CAD monitor probe, GETRSSI stream). The one deliberate
     * exception is CONFIG apply (client-initiated, rare, must not be silently
     * dropped) — see config_dispatch.cpp.
     */
    std::recursive_mutex radio_mutex;

    /* Atomic: health is written from the main loop (boot, CONFIG fail-
     * closed) and read/escalated from the TX worker (re-arm failures). */
    std::atomic<RadioHealth> health;
    RadioMode_t mode;
    std::atomic<bool> received;
    std::atomic<bool> rx_rearm_pending;
    /* Consecutive failed re-arms (P1-6): reset on success; at the threshold
     * the radio escalates to FAILED instead of staying READY-but-deaf. */
    std::atomic<uint32_t> rx_rearm_consecutive_failures;
    /* Next retry time (ms, monotonic); retries back off to 1 Hz instead of
     * hammering SPI every main-loop tick. Main-loop-only, no lock needed. */
    int64_t rx_rearm_next_retry_ms;
    std::atomic<bool> tx_busy;
    std::atomic<uint32_t> tx_status_generation;
    uint32_t tx_status_broadcast_generation;
    std::atomic<bool> cad_active;
    std::atomic<bool> cad_broadcast_active;
    std::atomic<bool> cad_monitor_active;
    std::atomic<int> cad_monitor_free_streak;
    std::atomic<float> cad_rssi_threshold_dbm;
    std::atomic<bool> getrssi_active;
    std::atomic<bool> tx_result_active;
    std::atomic<bool> tx_queue_active;
    RadioTxMode_t tx_mode;
    uint16_t tx_result_seq;

    std::atomic<uint32_t> cad_wait_timeout_ms;
    std::atomic<uint32_t> cad_idle_stable_ms;
    std::atomic<uint32_t> cad_poll_interval_ms;
    std::atomic<bool>     cad_send_after_timeout;

    /* Hardware capability (set once at init from the hardware profile):
     * false = scanChannel()-based CAD is unusable on this wiring (SX127x
     * without DIO1 would report false-FREE); probes fall back to the passive
     * RSSI probe. */
    bool cad_scan_available;

    /* STATUS witnesses (set once at init, never by CONF): the resolved chip
     * family of THIS process's radio, and whether it was started with
     * --high-power. A controller reads them off the running daemon instead of
     * trusting what it stored -- a hardware setup saved after the process
     * started is not what is running. */
    DaemonChipFamily chip_family;
    bool high_power_enabled;

    DaemonRadioStats stats;

    void (*rx_callback)(void);

    int led_pin;
    std::mutex led_mutex;
    int led_state;
};

static inline void radio_controller_init(RadioController *ctrl,
                                         RadioBand_t band,
                                         const char *tag,
                                         bool is_hf,
                                         void (*rx_callback)(void),
                                         int led_pin)
{
    if (!ctrl)
        return;

    ctrl->band = band;
    ctrl->tag = tag;
    ctrl->is_hf = is_hf;

    ctrl->hal.reset();
    ctrl->mod.reset();
    ctrl->driver.reset();

    ctrl->health = RADIO_HEALTH_UNINITIALIZED;
    ctrl->mode = RADIO_MODE_LORA;
    ctrl->received.store(false);
    ctrl->rx_rearm_pending.store(false);
    ctrl->rx_rearm_consecutive_failures.store(0);
    ctrl->rx_rearm_next_retry_ms = 0;
    ctrl->tx_busy.store(false);
    ctrl->tx_status_generation.store(0u);
    ctrl->tx_status_broadcast_generation = 0u;
    ctrl->cad_active.store(false);
    ctrl->cad_broadcast_active.store(false);
    ctrl->cad_monitor_active.store(false);
    ctrl->cad_monitor_free_streak.store(0);
    ctrl->cad_rssi_threshold_dbm.store(RADIO_CAD_RSSI_BUSY_THRESHOLD_DBM);
    ctrl->getrssi_active.store(false);
    ctrl->tx_result_active.store(false);
    ctrl->tx_queue_active.store(true);
    ctrl->tx_mode = RADIO_TX_MODE_MANAGED;
    ctrl->tx_result_seq = 0;

    ctrl->cad_wait_timeout_ms.store(DAEMON_TX_POLICY_CAD_WAIT_TIMEOUT_MS);
    ctrl->cad_idle_stable_ms.store(DAEMON_TX_POLICY_CAD_IDLE_STABLE_MS);
    ctrl->cad_poll_interval_ms.store(DAEMON_TX_POLICY_POLL_INTERVAL_MS);
    ctrl->cad_send_after_timeout.store(
        DAEMON_TX_POLICY_SEND_AFTER_CAD_TIMEOUT ? true : false);
    ctrl->cad_scan_available = true;
    ctrl->chip_family = DAEMON_CHIP_FAMILY_SX127X;
    ctrl->high_power_enabled = false;

    daemon_radio_stats_init(&ctrl->stats);

    ctrl->rx_callback = rx_callback;
    ctrl->led_pin = led_pin;
    ctrl->led_state = -1;
}

static inline int radio_controller_band_number(const RadioController *ctrl)
{
    return ctrl ? (int)ctrl->band : 0;
}

static inline const char *radio_controller_tag(const RadioController *ctrl)
{
    if (!ctrl || !ctrl->tag)
        return "?";

    return ctrl->tag;
}

static inline RadioHealth radio_controller_health(const RadioController *ctrl)
{
    return ctrl ? ctrl->health.load() : RADIO_HEALTH_FAILED;
}

static inline bool radio_controller_ready(const RadioController *ctrl)
{
    return radio_health_is_ready(radio_controller_health(ctrl));
}

static inline RadioMode_t radio_controller_mode(const RadioController *ctrl)
{
    return ctrl ? ctrl->mode : RADIO_MODE_LORA;
}

static inline float radio_controller_packet_rssi(RadioController *ctrl)
{
    if (!ctrl || !ctrl->driver)
        return -200.0f;

    std::lock_guard<std::recursive_mutex> radio_lock(ctrl->radio_mutex);
    return ctrl->driver->getRSSI();
}

#endif
