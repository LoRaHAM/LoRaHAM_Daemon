#include "daemon_log.h"

#include <stdarg.h>
#include <stdio.h>

DaemonLogLevel daemon_log_level = DAEMON_LOG_NORMAL;

bool daemon_debug_enabled(void)
{
    return daemon_log_level >= DAEMON_LOG_DEBUG;
}

static void daemon_vlog(const char *prefix, const char *fmt, va_list ap)
{
    printf("%s ", prefix);
    vprintf(fmt, ap);
    printf("\n");
}

void daemon_log(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    daemon_vlog("[Daemon]", fmt, ap);
    va_end(ap);
}

static void daemon_vlog_ctx(const char *ctx, const char *fmt, va_list ap)
{
    char prefix[32];

    snprintf(prefix, sizeof(prefix), "[%s]", ctx ? ctx : "?");
    daemon_vlog(prefix, fmt, ap);
}

void daemon_debug_ctx(const char *ctx, const char *fmt, ...)
{
    va_list ap;

    if (!daemon_debug_enabled())
        return;

    va_start(ap, fmt);
    daemon_vlog_ctx(ctx, fmt, ap);
    va_end(ap);
}

void daemon_debug_band(const char *tag, const char *fmt, ...)
{
    va_list ap;

    if (!daemon_debug_enabled())
        return;

    va_start(ap, fmt);
    daemon_vlog_ctx(tag, fmt, ap);
    va_end(ap);
}
