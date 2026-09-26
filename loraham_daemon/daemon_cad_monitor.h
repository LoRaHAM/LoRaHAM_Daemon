#ifndef LORAHAM_DAEMON_CAD_MONITOR_H
#define LORAHAM_DAEMON_CAD_MONITOR_H

#include "daemon_monitoring.h"
#include "radio_cad.h"
#include "radio_controller.h"

/* --- CAD monitor per-band tick ------------------------------------------- */
/*
 * One sample of the opt-in CAD=0/1 CONF monitor: passive RSSI probe, then the
 * de-flicker state machine from daemon_monitoring.h. Updates the published
 * latch (cad_broadcast_active) and the free-confirmation streak; the caller
 * broadcasts, logs, and reconciles the LED based on the returned edge.
 *
 * The RX received flag is deliberately not consulted: a pending packet must
 * never suppress or delay the falling CAD=0 edge.
 */

typedef struct {
    int edge;       /* +1 broadcast CAD=1, -1 broadcast CAD=0, 0 nothing */
    float rssi_dbm; /* live RSSI sampled by the passive probe */
    int sampled;    /* 0 if the probe was unavailable (state untouched) */
} DaemonCadMonitorTick;

DaemonCadMonitorTick daemon_cad_monitor_tick(
    RadioController *ctrl);

#endif
