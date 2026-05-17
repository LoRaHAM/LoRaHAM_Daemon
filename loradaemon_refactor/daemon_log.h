#ifndef LORAHAM_DAEMON_LOG_H
#define LORAHAM_DAEMON_LOG_H

#include <stdarg.h>
#include <stdio.h>

typedef enum {
    DAEMON_LOG_NORMAL = 0,
    DAEMON_LOG_VERBOSE = 1,
    DAEMON_LOG_DEBUG = 2
} DaemonLogLevel;

static DaemonLogLevel daemon_log_level = DAEMON_LOG_NORMAL;

static bool daemon_verbose_enabled(void)
{
    return daemon_log_level >= DAEMON_LOG_VERBOSE;
}

static bool daemon_debug_enabled(void)
{
    return daemon_log_level >= DAEMON_LOG_DEBUG;
}

static void daemon_vlog(const char *prefix, const char *fmt, va_list ap)
{
    printf("%s ", prefix);
    vprintf(fmt, ap);
    printf("\n");
}

static void daemon_log(const char *fmt, ...)
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

static void daemon_verbose_ctx(const char *ctx, const char *fmt, ...)
{
    va_list ap;

    if (!daemon_verbose_enabled())
        return;

    va_start(ap, fmt);
    daemon_vlog_ctx(ctx, fmt, ap);
    va_end(ap);
}

static void daemon_debug_ctx(const char *ctx, const char *fmt, ...)
{
    va_list ap;

    if (!daemon_debug_enabled())
        return;

    va_start(ap, fmt);
    daemon_vlog_ctx(ctx, fmt, ap);
    va_end(ap);
}

static void daemon_debug_band(const char *tag, const char *fmt, ...)
{
    va_list ap;

    if (!daemon_debug_enabled())
        return;

    va_start(ap, fmt);
    daemon_vlog_ctx(tag, fmt, ap);
    va_end(ap);
}

#endif
