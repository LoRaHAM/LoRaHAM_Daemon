#include "daemon_radio_selection.h"

#include <string.h>

/* --- Radio selection ----------------------------------------------------- */

DaemonRadioSelection daemon_radio_selection = DAEMON_RADIO_SELECTION_BOTH;

const char *daemon_radio_selection_name(DaemonRadioSelection selection)
{
    switch (selection) {
        case DAEMON_RADIO_SELECTION_BOTH:
            return "both";
        case DAEMON_RADIO_SELECTION_433:
            return "433";
        case DAEMON_RADIO_SELECTION_868:
            return "868";
    }

    return "unknown";
}

bool daemon_radio_433_enabled(void)
{
    return daemon_radio_selection == DAEMON_RADIO_SELECTION_BOTH ||
           daemon_radio_selection == DAEMON_RADIO_SELECTION_433;
}

bool daemon_radio_868_enabled(void)
{
    return daemon_radio_selection == DAEMON_RADIO_SELECTION_BOTH ||
           daemon_radio_selection == DAEMON_RADIO_SELECTION_868;
}

bool daemon_parse_radio_selection(const char *arg)
{
    if (!arg)
        return false;

    if (strcmp(arg, "both") == 0) {
        daemon_radio_selection = DAEMON_RADIO_SELECTION_BOTH;
        return true;
    }

    if (strcmp(arg, "433") == 0) {
        daemon_radio_selection = DAEMON_RADIO_SELECTION_433;
        return true;
    }

    if (strcmp(arg, "868") == 0) {
        daemon_radio_selection = DAEMON_RADIO_SELECTION_868;
        return true;
    }

    return false;
}
