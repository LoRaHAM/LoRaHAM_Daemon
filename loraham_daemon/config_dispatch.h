#ifndef LORAHAM_CONFIG_DISPATCH_H
#define LORAHAM_CONFIG_DISPATCH_H

#include "client_slot.h"
#include "config_apply.h"
#include "config_status.h"
#include "daemon_protocol.h"
#include "radio_channel.h"
#include "radio_controller.h"
#include "radio_health.h"

#include <stddef.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

/* --- CONFIG client dispatch -------------------------------------------- */

typedef void (*ConfigDispatchLogFn)(void *ctx, const char *msg);
typedef void (*ConfigDispatchLineLogFn)(void *ctx,
                                        const char *msg,
                                        const char *line);

typedef struct {
    void *ctx;
    ConfigDispatchLogFn message;
    ConfigDispatchLineLogFn line;
} ConfigDispatchLog;

static inline void config_dispatch_log_message(const ConfigDispatchLog *log,
                                               const char *msg)
{
    if (log && log->message)
        log->message(log->ctx, msg);
}

static inline void config_dispatch_log_line(const ConfigDispatchLog *log,
                                            const char *msg,
                                            const char *line)
{
    if (log && log->line)
        log->line(log->ctx, msg, line);
}

void config_dispatch_log_bytes(const ConfigDispatchLog *log,
                                             ssize_t n);

void config_dispatch_log_slot(const ConfigDispatchLog *log,
                                            int index,
                                            const char *msg);

struct ConfigDispatchContext {
    ClientSlot *slots;
    RadioController *ctrl;
    const char *tag;
    ConfigApplyFn apply_config;
    ConfigDispatchLog log;
};

struct ConfigLineApplyContext {
    ClientSlot *slot;
    RadioController *ctrl;
    const char *tag;
    ConfigApplyFn apply_config;
    ConfigDispatchLog log;
};

void config_dispatch_apply_line(const char *line, void *user);

void config_dispatch_client(ClientSlot *slots,
                                          int index,
                                          const EventLoopReadySet *readfds,
                                          uint8_t *buf,
                                          RadioController *ctrl,
                                          const char *tag,
                                          ConfigApplyFn apply_config,
                                          ConfigDispatchLog log);

void config_dispatch_clients(ClientSlot *slots,
                                           int max_clients,
                                           const EventLoopReadySet *readfds,
                                           uint8_t *buf,
                                           RadioController *ctrl,
                                           const char *tag,
                                           ConfigApplyFn apply_config,
                                           ConfigDispatchLog log);

void config_dispatch_context(ConfigDispatchContext *ctx,
                                           int max_clients,
                                           const EventLoopReadySet *readfds,
                                           uint8_t *buf);

#endif
