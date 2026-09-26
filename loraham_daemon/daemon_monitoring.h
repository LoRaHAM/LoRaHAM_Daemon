#ifndef LORAHAM_DAEMON_MONITORING_H
#define LORAHAM_DAEMON_MONITORING_H

#include <stdint.h>

#include "daemon_timing.h"

/* --- CAD/RSSI/stats monitoring ----------------------------------------- */

static inline int daemon_monitoring_cad_probe_due(
    DaemonDeadlineTimer *cad_timer,
    DaemonTimeMs now_ms,
    int has_conf_clients)
{
    if (!cad_timer || !has_conf_clients)
        return 0;

    return daemon_deadline_timer_due(cad_timer, now_ms);
}

static inline int daemon_monitoring_cad_broadcast_edge(
    int was_active,
    int hardware_active)
{
    if (!!was_active == !!hardware_active)
        return 0;

    return hardware_active ? 1 : -1;
}

/*
 * CAD monitor de-flicker policy: busy asserts immediately at or above the
 * threshold; a published busy state clears only after
 * DAEMON_CAD_FREE_CONFIRM_SAMPLES consecutive samples at least
 * DAEMON_CAD_FREE_HYSTERESIS_DB below it. Samples in the dead band between
 * the two levels retain the published state and cancel an incomplete free
 * confirmation.
 */
#define DAEMON_CAD_FREE_HYSTERESIS_DB   3.0f
#define DAEMON_CAD_FREE_CONFIRM_SAMPLES 2

/* Returns the new published busy state (0/1); maintains *free_streak. */
static inline int daemon_monitoring_cad_next_busy(
    int published_busy,
    float rssi_dbm,
    float threshold_dbm,
    int *free_streak)
{
    if (rssi_dbm >= threshold_dbm) {
        *free_streak = 0;
        return 1;
    }

    if (!published_busy) {
        *free_streak = 0;
        return 0;
    }

    if (rssi_dbm <= threshold_dbm - DAEMON_CAD_FREE_HYSTERESIS_DB) {
        (*free_streak)++;
        if (*free_streak >= DAEMON_CAD_FREE_CONFIRM_SAMPLES) {
            *free_streak = 0;
            return 0;
        }
        return 1;
    }

    *free_streak = 0;
    return 1;
}

static inline int daemon_monitoring_tx_status_next_busy(
    uint32_t observed_generation)
{
    return (observed_generation & 1u) == 0u;
}

void daemon_process_monitoring(DaemonDeadlineTimer *cad_timer,
                                DaemonDeadlineTimer *rssi_timer,
                                DaemonDeadlineTimer *stats_timer);

#endif
