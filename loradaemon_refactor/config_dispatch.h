#ifndef LORAHAM_CONFIG_DISPATCH_H
#define LORAHAM_CONFIG_DISPATCH_H

#include "client_set.h"
#include "config_apply.h"
#include "config_stream.h"
#include "daemon_protocol.h"
#include "radio_channel.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

/* --- CONFIG client dispatch -------------------------------------------- */

template<typename RadioT>
struct ConfigDispatchContext {
    int *clients;
    ConfigStreamBuffer *streams;
    RadioT *radio;
    const char *tag;
    const char *prefix;
    volatile RadioMode_t *mode;
    volatile bool *getrssi_active;
    ConfigApplyFn<RadioT> apply_config;
    void (*rx_callback)(void);
};

template<typename RadioT>
struct ConfigLineApplyContext {
    RadioT *radio;
    const char *tag;
    const char *prefix;
    volatile RadioMode_t *mode;
    volatile bool *getrssi_active;
    ConfigApplyFn<RadioT> apply_config;
    void (*rx_callback)(void);
};

template<typename RadioT>
static void config_dispatch_apply_line(const char *line, void *user)
{
    ConfigLineApplyContext<RadioT> *ctx =
        (ConfigLineApplyContext<RadioT> *)user;

    if(ctx->prefix)
        printf("%s", ctx->prefix);

    ctx->apply_config(*ctx->radio, ctx->tag, line,
                      *ctx->mode, *ctx->getrssi_active);

    // beginFSK()/begin() loescht den IRQ-Callback.
    ctx->radio->setPacketReceivedAction(ctx->rx_callback);
    ctx->radio->startReceive();
}

template<typename RadioT>
static void config_dispatch_client(int *clients,
                                   ConfigStreamBuffer *streams,
                                   int index,
                                   const EventLoopReadySet *readfds,
                                   uint8_t *buf,
                                   RadioT& radio,
                                   const char *tag,
                                   const char *prefix,
                                   volatile RadioMode_t& mode,
                                   volatile bool& getrssi_active,
                                   ConfigApplyFn<RadioT> apply_config,
                                   void (*rx_callback)(void))
{
    if(!client_set_slot_ready(clients, index, readfds))
        return;

    ConfigLineApplyContext<RadioT> line_ctx = {
        &radio,
        tag,
        prefix,
        &mode,
        &getrssi_active,
        apply_config,
        rx_callback
    };

    ssize_t n = read(clients[index], buf, buf_SIZE - 1);
    if(n < 0) {
        config_stream_init(&streams[index]);
        client_set_close_slot(clients, index);
        return;
    }

    if(n == 0) {
        if(config_stream_flush(&streams[index],
                               config_dispatch_apply_line<RadioT>,
                               &line_ctx) != 0) {
            printf("[%s] CONFIG stream flush error\n", tag);
            fflush(stdout);
        }

        config_stream_init(&streams[index]);
        client_set_close_slot(clients, index);
        return;
    }

    if(config_stream_feed(&streams[index], buf, (size_t)n,
                          config_dispatch_apply_line<RadioT>,
                          &line_ctx) != 0) {
        printf("[%s] CONFIG stream too long, client closed\n", tag);
        fflush(stdout);
        config_stream_init(&streams[index]);
        client_set_close_slot(clients, index);
        return;
    }
}

template<typename RadioT>
static void config_dispatch_clients(int *clients,
                                    ConfigStreamBuffer *streams,
                                    int max_clients,
                                    const EventLoopReadySet *readfds,
                                    uint8_t *buf,
                                    RadioT& radio,
                                    const char *tag,
                                    const char *prefix,
                                    volatile RadioMode_t& mode,
                                    volatile bool& getrssi_active,
                                    ConfigApplyFn<RadioT> apply_config,
                                    void (*rx_callback)(void))
{
    for(int i=0;i<max_clients;i++){
        config_dispatch_client<RadioT>(clients, streams, i, readfds, buf,
                                       radio, tag, prefix,
                                       mode, getrssi_active,
                                       apply_config, rx_callback);
    }
}

template<typename RadioT>
static void config_dispatch_context(ConfigDispatchContext<RadioT> *ctx,
                                    int max_clients,
                                    const EventLoopReadySet *readfds,
                                    uint8_t *buf)
{
    config_dispatch_clients<RadioT>(ctx->clients, ctx->streams,
                                    max_clients, readfds, buf,
                                    *ctx->radio, ctx->tag, ctx->prefix,
                                    *ctx->mode, *ctx->getrssi_active,
                                    ctx->apply_config, ctx->rx_callback);
}

#endif
