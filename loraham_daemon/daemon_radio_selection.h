#ifndef LORAHAM_DAEMON_RADIO_SELECTION_H
#define LORAHAM_DAEMON_RADIO_SELECTION_H

/* --- Radio selection ----------------------------------------------------- */
/*
 * One radio per process: the selection starts UNSET and must be set to 433 or
 * 868 via --radio before startup may proceed. Dual-band deployments run two
 * processes (loraham-daemon@433 + loraham-daemon@868).
 */

typedef enum {
    DAEMON_RADIO_SELECTION_UNSET = 0,
    DAEMON_RADIO_SELECTION_433,
    DAEMON_RADIO_SELECTION_868
} DaemonRadioSelection;

extern DaemonRadioSelection daemon_radio_selection;

const char *daemon_radio_selection_name(DaemonRadioSelection selection);
bool daemon_radio_selection_is_set(void);
bool daemon_radio_433_enabled(void);
bool daemon_radio_868_enabled(void);
bool daemon_parse_radio_selection(const char *arg);

#endif
