#ifndef LORAHAM_CONFIG_DISPATCH_H
#define LORAHAM_CONFIG_DISPATCH_H

#include "client_slot.h"
#include "config_apply.h"
#include "daemon_protocol.h"
#include "radio_channel.h"
#include "radio_controller.h"
#include "radio_health.h"

#include <stddef.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
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

static inline void config_dispatch_log_bytes(const ConfigDispatchLog *log,
                                             ssize_t n)
{
    char msg[64];

    snprintf(msg, sizeof(msg), "%zd Byte empfangen", n);
    config_dispatch_log_message(log, msg);
}

static inline void config_dispatch_log_slot(const ConfigDispatchLog *log,
                                            int index,
                                            const char *msg)
{
    char line[80];

    snprintf(line, sizeof(line), "Slot %d: %s", index, msg);
    config_dispatch_log_message(log, line);
}

template<typename RadioT>
struct ConfigDispatchContext {
    ClientSlot *slots;
    RadioController<RadioT> *ctrl;
    const char *tag;
    const char *prefix;
    ConfigApplyFn<RadioT> apply_config;
    ConfigDispatchLog log;
};

template<typename RadioT>
struct ConfigLineApplyContext {
    RadioController<RadioT> *ctrl;
    const char *tag;
    const char *prefix;
    ConfigApplyFn<RadioT> apply_config;
    ConfigDispatchLog log;
};

template<typename RadioT>
static void config_dispatch_apply_line(const char *line, void *user)
{
    ConfigLineApplyContext<RadioT> *ctx =
        (ConfigLineApplyContext<RadioT> *)user;

    config_dispatch_log_line(&ctx->log, "Zeile", line);

    if(!ctx->ctrl || !ctx->ctrl->radio ||
       !radio_controller_ready(ctx->ctrl)) {
        config_dispatch_log_message(&ctx->log, "Radio nicht bereit");

        if(ctx->prefix)
            printf("%s", ctx->prefix);

        printf(" RADIO=%s CONFIG ignored\n",
               radio_health_name(radio_controller_health(ctx->ctrl)));
        fflush(stdout);
        return;
    }

    config_dispatch_log_message(&ctx->log, "Apply startet");

    if(ctx->prefix)
        printf("%s", ctx->prefix);

    ctx->apply_config(*ctx->ctrl->radio, ctx->tag, line,
                      ctx->ctrl->mode, ctx->ctrl->getrssi_active);

    // beginFSK()/begin() loescht den IRQ-Callback.
    ctx->ctrl->radio->setPacketReceivedAction(ctx->ctrl->rx_callback);
    config_dispatch_log_message(&ctx->log, "Callback neu gesetzt");
    ctx->ctrl->radio->startReceive();
    config_dispatch_log_message(&ctx->log, "RX neu gestartet");
}

template<typename RadioT>
static void config_dispatch_client(ClientSlot *slots,
                                   int index,
                                   const EventLoopReadySet *readfds,
                                   uint8_t *buf,
                                   RadioController<RadioT> *ctrl,
                                   const char *tag,
                                   const char *prefix,
                                   ConfigApplyFn<RadioT> apply_config,
                                   ConfigDispatchLog log)
{
    ClientSlot *slot = &slots[index];

    if(!client_slot_ready(slot, readfds))
        return;

    ConfigLineApplyContext<RadioT> line_ctx = {
        ctrl,
        tag,
        prefix,
        apply_config,
        log
    };

    config_dispatch_log_slot(&log, index, "Client bereit");

    ssize_t n;

    do {
        n = read(slot->fd, buf, buf_SIZE - 1);
    } while(n < 0 && errno == EINTR);

    if(n < 0) {
        if(errno == EAGAIN || errno == EWOULDBLOCK)
            return;

        config_dispatch_log_slot(&log, index, "Lesefehler, Client zu");
        client_slot_close(slot);
        return;
    }

    if(n == 0) {
        config_dispatch_log_slot(&log, index, "EOF, Stream flush");
        if(config_stream_flush(&slot->stream,
                               config_dispatch_apply_line<RadioT>,
                               &line_ctx) != 0) {
            config_dispatch_log_slot(&log, index, "Flush-Fehler");
            printf("[%s] CONFIG stream flush error\n", tag);
            fflush(stdout);
        }

        client_slot_close(slot);
        config_dispatch_log_slot(&log, index, "Client geschlossen");
        return;
    }

    config_dispatch_log_bytes(&log, n);

    if(config_stream_feed(&slot->stream, buf, (size_t)n,
                          config_dispatch_apply_line<RadioT>,
                          &line_ctx) != 0) {
        config_dispatch_log_slot(&log, index, "Stream zu lang, Client zu");
        printf("[%s] CONFIG stream too long, client closed\n", tag);
        fflush(stdout);
        client_slot_close(slot);
        return;
    }
}

template<typename RadioT>
static void config_dispatch_clients(ClientSlot *slots,
                                    int max_clients,
                                    const EventLoopReadySet *readfds,
                                    uint8_t *buf,
                                    RadioController<RadioT> *ctrl,
                                    const char *tag,
                                    const char *prefix,
                                    ConfigApplyFn<RadioT> apply_config,
                                    ConfigDispatchLog log)
{
    for(int i=0;i<max_clients;i++){
        config_dispatch_client<RadioT>(slots, i, readfds, buf,
                                       ctrl, tag, prefix,
                                       apply_config, log);
    }
}

template<typename RadioT>
static void config_dispatch_context(ConfigDispatchContext<RadioT> *ctx,
                                    int max_clients,
                                    const EventLoopReadySet *readfds,
                                    uint8_t *buf)
{
    config_dispatch_clients<RadioT>(ctx->slots,
                                    max_clients, readfds, buf,
                                    ctx->ctrl, ctx->tag, ctx->prefix,
                                    ctx->apply_config,
                                    ctx->log);
}

#endif
