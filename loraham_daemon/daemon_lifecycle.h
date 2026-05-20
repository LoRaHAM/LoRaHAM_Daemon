#ifndef LORAHAM_DAEMON_LIFECYCLE_H
#define LORAHAM_DAEMON_LIFECYCLE_H

/* --- daemon stop request --- */

void daemon_lifecycle_reset_stop(void);
void daemon_lifecycle_request_stop(int signal_number);
int daemon_lifecycle_stop_requested(void);
int daemon_lifecycle_install_signal_handlers(void);

#endif
