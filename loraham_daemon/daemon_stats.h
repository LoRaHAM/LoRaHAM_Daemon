#ifndef LORAHAM_DAEMON_STATS_H
#define LORAHAM_DAEMON_STATS_H

#include <stddef.h>

#include "daemon_timing.h"
#include "radio_health.h"
#include "tx_result.h"

/* --- Runtime statistics -------------------------------------------------- */

#define DAEMON_STATS_LOG_INTERVAL_MS INT64_C(3600000)

typedef struct {
    unsigned long rx_packets;
    unsigned long rx_bytes;
    unsigned long rx_drops;
    unsigned long tx_ok;
    unsigned long tx_errors;
    unsigned long tx_busy;
    unsigned long cad_timeouts;
    unsigned long cad_timeout_sends;
    unsigned long rx_rearm_failures;
} DaemonRadioStats;

void daemon_radio_stats_init(DaemonRadioStats *stats);
void daemon_radio_stats_record_rx(DaemonRadioStats *stats, size_t bytes);
void daemon_radio_stats_record_rx_drop(DaemonRadioStats *stats);
void daemon_radio_stats_record_tx_result(DaemonRadioStats *stats,
                                         TxResult result);
void daemon_radio_stats_record_cad_timeout(DaemonRadioStats *stats);
void daemon_radio_stats_record_cad_timeout_send(DaemonRadioStats *stats);
void daemon_radio_stats_record_rx_rearm_failure(DaemonRadioStats *stats);

void daemon_stats_start(DaemonTimeMs now_ms);
long daemon_stats_uptime_seconds(DaemonTimeMs now_ms);

void daemon_stats_format_fields(char *buf,
                                size_t buf_size,
                                long uptime_seconds,
                                RadioHealth health,
                                const DaemonRadioStats *stats);
void daemon_stats_format_response(char *buf,
                                  size_t buf_size,
                                  long uptime_seconds,
                                  RadioHealth health,
                                  const DaemonRadioStats *stats);

#endif
