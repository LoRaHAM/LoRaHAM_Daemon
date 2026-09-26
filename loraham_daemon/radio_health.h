#ifndef LORAHAM_RADIO_HEALTH_H
#define LORAHAM_RADIO_HEALTH_H

/* --- Radio health state --- */

typedef enum {
    RADIO_HEALTH_UNINITIALIZED = 0,
    RADIO_HEALTH_READY,
    RADIO_HEALTH_FAILED
} RadioHealth;

const char *radio_health_name(RadioHealth health);
int radio_health_is_ready(RadioHealth health);

#endif
