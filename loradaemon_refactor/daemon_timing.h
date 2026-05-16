#ifndef LORAHAM_DAEMON_TIMING_H
#define LORAHAM_DAEMON_TIMING_H

/* --- Current select-loop timing --- */

#define DAEMON_SELECT_TIMEOUT_USEC 10000

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
    long interval_ms;
    long next_due_ms;
} DaemonDeadlineTimer;

void daemon_deadline_timer_init(DaemonDeadlineTimer *timer,
                                long now_ms,
                                long interval_ms);
long daemon_deadline_timer_timeout_ms(const DaemonDeadlineTimer *timer,
                                      long now_ms);
int daemon_deadline_timer_due(DaemonDeadlineTimer *timer,
                              long now_ms);

/* --- Monotonic time --- */

long daemon_now_ms(void);

#endif
