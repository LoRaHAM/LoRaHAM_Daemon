#ifndef LORAHAM_DAEMON_LOG_H
#define LORAHAM_DAEMON_LOG_H

typedef enum {
    DAEMON_LOG_NORMAL = 0,
    DAEMON_LOG_DEBUG = 1
} DaemonLogLevel;

extern DaemonLogLevel daemon_log_level;

bool daemon_debug_enabled(void);

void daemon_log(const char *fmt, ...);
void daemon_debug_ctx(const char *ctx, const char *fmt, ...);
void daemon_debug_band(const char *tag, const char *fmt, ...);

#endif
