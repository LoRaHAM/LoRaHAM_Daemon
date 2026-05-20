#include "daemon_lifecycle.h"

#include <signal.h>
#include <string.h>

/* --- Daemon stop handling ---------------------------------------------- */

static volatile sig_atomic_t daemon_stop_requested_flag = 0;

void daemon_lifecycle_reset_stop(void)
{
    daemon_stop_requested_flag = 0;
}

void daemon_lifecycle_request_stop(int signal_number)
{
    (void)signal_number;
    daemon_stop_requested_flag = 1;
}

int daemon_lifecycle_stop_requested(void)
{
    return daemon_stop_requested_flag ? 1 : 0;
}

int daemon_lifecycle_install_signal_handlers(void)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = daemon_lifecycle_request_stop;

    if (sigemptyset(&action.sa_mask) != 0)
        return -1;

    if (sigaction(SIGINT, &action, NULL) != 0)
        return -1;

    if (sigaction(SIGTERM, &action, NULL) != 0)
        return -1;

    return 0;
}
