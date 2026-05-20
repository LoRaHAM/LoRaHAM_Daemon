#include "daemon_timing.h"

#include <time.h>

/* --- Tick timers -------------------------------------------------------- */

int daemon_tick_due(int *counter, int interval)
{
    (*counter)++;

    if (*counter >= interval) {
        *counter = 0;
        return 1;
    }

    return 0;
}

void daemon_tick_init(DaemonTick *tick, int interval)
{
    tick->counter = 0;
    tick->interval = interval;
}

int daemon_tick_state_due(DaemonTick *tick)
{
    return daemon_tick_due(&tick->counter, tick->interval);
}

/* --- Deadline timers ---------------------------------------------------- */

void daemon_deadline_timer_init(DaemonDeadlineTimer *timer,
                                long now_ms,
                                long interval_ms)
{
    timer->interval_ms = interval_ms;
    timer->next_due_ms = now_ms + interval_ms;
}

long daemon_deadline_timer_timeout_ms(const DaemonDeadlineTimer *timer,
                                      long now_ms)
{
    if (now_ms >= timer->next_due_ms)
        return 0;

    return timer->next_due_ms - now_ms;
}

int daemon_deadline_timer_due(DaemonDeadlineTimer *timer,
                              long now_ms)
{
    if (now_ms < timer->next_due_ms)
        return 0;

    do {
        timer->next_due_ms += timer->interval_ms;
    } while (now_ms >= timer->next_due_ms);

    return 1;
}

/* --- Monotonic clock ---------------------------------------------------- */

long daemon_now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)(ts.tv_sec * 1000L + ts.tv_nsec / 1000000L);
}

