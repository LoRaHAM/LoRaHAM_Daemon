#ifndef LORAHAM_DAEMON_LIFECYCLE_H
#define LORAHAM_DAEMON_LIFECYCLE_H

/* --- daemon stop request --- */

void daemon_lifecycle_reset_stop(void);
void daemon_lifecycle_request_stop(int signal_number);
int daemon_lifecycle_stop_requested(void);
int daemon_lifecycle_install_signal_handlers(void);
void daemon_lifecycle_ignore_sigpipe(void);
int daemon_lifecycle_redirect_stdio(const char *log_path);
void daemon_lifecycle_enter_background(void);

/* --- event-wait error classification --- */
/*
 * EINTR without a pending stop request is benign (debugger attach, SIGCHLD
 * from a wrapper) and must not hit the perror path — one classification
 * point so the main loop cannot regress into per-signal log spam.
 */
typedef enum {
    DAEMON_WAIT_ERROR_SILENT = 0,   /* benign EINTR: continue, debug only */
    DAEMON_WAIT_ERROR_STOPPING = 1, /* EINTR during stop: exit the tick */
    DAEMON_WAIT_ERROR_LOG = 2       /* real error: perror + failure check */
} DaemonWaitErrorAction;

DaemonWaitErrorAction daemon_lifecycle_classify_wait_error(int err,
                                                           int stop_requested);

#endif
