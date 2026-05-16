#include "daemon_timing.h"

/* --- Counter-based tick helper --- */

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

