#include "radio_health.h"

/* --- Radio health helpers ------------------------------------------------ */

const char *radio_health_name(RadioHealth health)
{
    switch (health) {
        case RADIO_HEALTH_UNINITIALIZED:
            return "UNINITIALIZED";
        case RADIO_HEALTH_READY:
            return "READY";
        case RADIO_HEALTH_FAILED:
            return "FAILED";
    }

    return "UNKNOWN";
}

int radio_health_is_ready(RadioHealth health)
{
    return health == RADIO_HEALTH_READY;
}
