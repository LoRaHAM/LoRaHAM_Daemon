#ifndef LORAHAM_DAEMON_TIMING_H
#define LORAHAM_DAEMON_TIMING_H

#include <stdint.h>

/* --- Millisekunden-Zeittyp -------------------------------------------- */

typedef int64_t DaemonTimeMs;

/* --- Event-loop timing --- */

#define DAEMON_EVENT_LOOP_TIMEOUT_USEC 10000

/* --- Current RSSI stream cadence --- */

#define DAEMON_RSSI_TICK_INTERVAL 10
#define DAEMON_RSSI_INTERVAL_MS 100

/* --- Counter-based tick helper --- */

typedef struct {
    int counter;
    int interval;
} DaemonTick;

void daemon_tick_init(DaemonTick *tick, int interval);
int daemon_tick_state_due(DaemonTick *tick);

int daemon_tick_due(int *counter, int interval);

/* --- Deadline timer helper --- */

typedef struct {
    DaemonTimeMs interval_ms;
    DaemonTimeMs next_due_ms;
} DaemonDeadlineTimer;

void daemon_deadline_timer_init(DaemonDeadlineTimer *timer,
                                DaemonTimeMs now_ms,
                                DaemonTimeMs interval_ms);
DaemonTimeMs daemon_deadline_timer_timeout_ms(
    const DaemonDeadlineTimer *timer,
    DaemonTimeMs now_ms);
int daemon_deadline_timer_due(DaemonDeadlineTimer *timer,
                              DaemonTimeMs now_ms);

/* --- Monotonic time --- */

DaemonTimeMs daemon_now_ms(void);

#endif
