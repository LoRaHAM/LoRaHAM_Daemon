#include "daemon_monitoring.h"

#include <stdio.h>
#include <mutex>

#include "client_slot.h"
#include "daemon_band.h"
#include "daemon_io_runtime.h"
#include "daemon_log.h"
#include "daemon_protocol.h"
#include "daemon_radio_runtime.h"
#include "daemon_stats.h"
#include "radio_channel.h"
#include "radio_controller.h"
#include "radio_cad.h"
#include "daemon_cad_monitor.h"
#include "radio_health.h"

/* --- CAD/RSSI log contexts ---------------------------------------------- */
static const char *daemon_cad_log_ctx(RadioController *)
{
    return daemon_band()->cad_log_ctx;
}

static const char *daemon_rssi_log_ctx(RadioController *)
{
    return daemon_band()->rssi_log_ctx;
}



/* --- TX status ----------------------------------------------------------- */
static void daemon_process_tx_status(RadioController *ctrl,
                                     RadioChannelIo *io)
{
    uint32_t observed;
    uint32_t current;

    if (!ctrl || !io)
        return;

    observed = ctrl->tx_status_broadcast_generation;
    current = ctrl->tx_status_generation.load();

    while (observed != current) {
        int busy = daemon_monitoring_tx_status_next_busy(observed);

        observed++;
        ctrl->tx_status_broadcast_generation = observed;

        if (client_slot_has_clients(io->conf_slots, MAX_CLIENTS)) {
            client_slot_broadcast_queued(
                io->conf_slots,
                MAX_CLIENTS,
                busy ? "TX=1\n" : "TX=0\n");
        }
    }
}

/* --- CAD status ---------------------------------------------------------- */
static void daemon_process_cad_status(RadioController *ctrl,
                                      RadioChannelIo *io)
{
    if (!io || !client_slot_has_clients(io->conf_slots, MAX_CLIENTS))
        return;

    if (!radio_controller_ready(ctrl) || !ctrl->driver)
        return;

    if (ctrl->mode != RADIO_MODE_LORA)
        return;

    // Non-destructive RSSI probe only: the continuous monitoring tick must never
    // touch RX (no scanChannel, no startReceive). scanChannel-based CAD stays
    // for TX gating and GET CHANNEL. A pending RX packet must never suppress
    // the falling CAD=0 edge, so the tick ignores the received flag.
    DaemonCadMonitorTick tick = daemon_cad_monitor_tick(ctrl);
    const char *ctx = daemon_cad_log_ctx(ctrl);

    if (!tick.sampled)
        return;

    if (tick.edge > 0) {
        daemon_debug_ctx(ctx, "active cad=BUSY rssi=%.1f", tick.rssi_dbm);
        client_slot_broadcast_queued(io->conf_slots, MAX_CLIENTS, "CAD=1\n");
    } else if (tick.edge < 0) {
        daemon_debug_ctx(ctx, "inactive cad=FREE rssi=%.1f", tick.rssi_dbm);
        client_slot_broadcast_queued(io->conf_slots, MAX_CLIENTS, "CAD=0\n");
    }

    // The LED is derived from cad_broadcast_active (and tx_busy); reconcile it
    // immediately after the source-of-truth update.
    daemon_radio_runtime_sync_led(ctrl);
}
/* --- RSSI streaming ------------------------------------------------------- */
/*
 * GETRSSI streams live RSSI on the matching CONF socket.
 * RSSI comes from the driver's raw live-RSSI read (chip register) because
 * RadioLib getRSSI() reports the last packet RSSI in LoRa mode.
 *
 * Skip reads during TX. Auto-stop when no CONF client remains connected.
 */
static void daemon_radio_controller_getrssi_autostop(RadioChannelIo *io,
                                                     RadioController *ctrl)
{
    if(!client_slot_has_clients(io->conf_slots, MAX_CLIENTS) && ctrl->getrssi_active.load()) {
        const char *ctx = daemon_rssi_log_ctx(ctrl);

        ctrl->getrssi_active.store(false);
        daemon_debug_ctx(ctx, "auto-stop: no client");
    }
}

static void daemon_process_rssi_stream_one(RadioController *ctrl,
                                           RadioChannelIo *io)
{
    const char *ctx = daemon_rssi_log_ctx(ctrl);

    if (!ctrl->getrssi_active.load())
        return;

    if (ctrl->tx_busy.load()) {
        daemon_debug_ctx(ctx, "TX active, skipping");
        return;
    }

    if (!radio_controller_ready(ctrl) || !ctrl->driver) {
        daemon_debug_ctx(ctx, "radio not ready");
        return;
    }

    /* Lock discipline (see radio_controller.h): main-loop tick — never block
     * behind a TX holding radio_mutex; skip this tick on contention. */
    std::unique_lock<std::recursive_mutex> radio_lock(
        ctrl->radio_mutex, std::try_to_lock);
    if (!radio_lock.owns_lock()) {
        daemon_debug_ctx(ctx, "radio busy, skipping");
        return;
    }

    float rssi = ctrl->driver->readLiveRssi(ctrl->mode, ctrl->is_hf);
    char rssi_msg[32];
    snprintf(rssi_msg, sizeof(rssi_msg), "RSSI=%.2f\n", rssi);
    daemon_debug_ctx(ctx, "sending %.2f dBm", rssi);
    client_slot_broadcast_queued(io->conf_slots, MAX_CLIENTS, rssi_msg);
}

static void daemon_process_rssi_stream(DaemonDeadlineTimer *rssi_timer)
{
    daemon_radio_controller_getrssi_autostop(&channel, &radio_controller);

    // RSSI streaming is time based.
    if (daemon_deadline_timer_due(rssi_timer, daemon_now_ms()))
        daemon_process_rssi_stream_one(&radio_controller, &channel);
}

/* --- Periodic operator stats -------------------------------------------- */
static void daemon_print_radio_stats(RadioController *ctrl)
{
    char fields[384];
    long uptime = daemon_stats_uptime_seconds(daemon_now_ms());

    daemon_stats_format_fields(fields,
                               sizeof(fields),
                               uptime,
                               radio_controller_health(ctrl),
                               &ctrl->stats);

    printf("[STATS %s] %s\n", radio_controller_tag(ctrl), fields);
    fflush(stdout);
}

static void daemon_process_periodic_stats(DaemonDeadlineTimer *stats_timer)
{
    if (!daemon_deadline_timer_due(stats_timer, daemon_now_ms()))
        return;

    daemon_print_radio_stats(&radio_controller);
}

/* --- CAD/RSSI polling ---------------------------------------------------- */
static void daemon_process_cad_rssi(DaemonDeadlineTimer *cad_timer,
                                    DaemonDeadlineTimer *rssi_timer)
{
    // Monitoring runs only on explicit opt-in (SET CADMONITOR=1). A merely
    // connected CONF client (e.g. a config-only MeshCom client) must not trigger
    // it, so it can never disturb RX.
    bool cad_subscribed =
        client_slot_has_clients(channel.conf_slots, MAX_CLIENTS) &&
        radio_controller.cad_monitor_active.load();

    if (daemon_monitoring_cad_probe_due(cad_timer,
                                        daemon_now_ms(),
                                        cad_subscribed)) {
        if (cad_subscribed)
            daemon_process_cad_status(&radio_controller, &channel);
    }

    // When not (or no longer) subscribed the band must not keep a stale busy
    // latch: the falling edge in daemon_process_cad_status only runs while
    // subscribed, so a last CONF disconnect in the busy state would otherwise
    // leave cad_broadcast_active=true (LED latched, stale CAD=1). Clear it and
    // reconcile the LED; cad_monitor_active stays armed for a reconnect.
    if (!cad_subscribed) {
        radio_controller.cad_broadcast_active.store(false);
        radio_controller.cad_monitor_free_streak.store(0);
        daemon_radio_runtime_sync_led(&radio_controller);
    }

    daemon_process_rssi_stream(rssi_timer);
}

/* --- Monitoring tick ----------------------------------------------------- */
void daemon_process_monitoring(DaemonDeadlineTimer *cad_timer,
                                DaemonDeadlineTimer *rssi_timer,
                                DaemonDeadlineTimer *stats_timer)
{
    daemon_process_tx_status(&radio_controller, &channel);
    daemon_process_cad_rssi(cad_timer, rssi_timer);
    daemon_process_periodic_stats(stats_timer);

    // Safety net: drive the LED from the derived state every loop, so a
    // missed edge can never leave the pin latched.
    daemon_radio_runtime_sync_led(&radio_controller);
}
