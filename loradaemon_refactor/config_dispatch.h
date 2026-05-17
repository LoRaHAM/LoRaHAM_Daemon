#ifndef LORAHAM_CONFIG_DISPATCH_H
#define LORAHAM_CONFIG_DISPATCH_H

#include "client_slot.h"
#include "config_apply.h"
#include "daemon_protocol.h"
#include "radio_channel.h"
#include "radio_health.h"

#include <stddef.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

/* --- CONFIG client dispatch -------------------------------------------- */

template<typename RadioT>
struct ConfigDispatchContext {
    ClientSlot *slots;
    RadioT *radio;
    volatile RadioHealth *health;
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
    volatile RadioHealth *health;
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

    if(!radio_health_is_ready(*ctx->health)) {
        printf(" RADIO=%s CONFIG ignored\n",
               radio_health_name(*ctx->health));
        fflush(stdout);
        return;
    }

    ctx->apply_config(*ctx->radio, ctx->tag, line,
                      *ctx->mode, *ctx->getrssi_active);

    // beginFSK()/begin() loescht den IRQ-Callback.
    ctx->radio->setPacketReceivedAction(ctx->rx_callback);
    ctx->radio->startReceive();
}

template<typename RadioT>
static void config_dispatch_client(ClientSlot *slots,
                                   int index,
                                   const EventLoopReadySet *readfds,
                                   uint8_t *buf,
                                   RadioT& radio,
                                   volatile RadioHealth& health,
                                   const char *tag,
                                   const char *prefix,
                                   volatile RadioMode_t& mode,
                                   volatile bool& getrssi_active,
                                   ConfigApplyFn<RadioT> apply_config,
                                   void (*rx_callback)(void))
{
    ClientSlot *slot = &slots[index];

    if(!client_slot_ready(slot, readfds))
        return;

    ConfigLineApplyContext<RadioT> line_ctx = {
        &radio,
        &health,
        tag,
        prefix,
        &mode,
        &getrssi_active,
        apply_config,
        rx_callback
    };

    ssize_t n;

    do {
        n = read(slot->fd, buf, buf_SIZE - 1);
    } while(n < 0 && errno == EINTR);

    if(n < 0) {
        if(errno == EAGAIN || errno == EWOULDBLOCK)
            return;

        client_slot_close(slot);
        return;
    }

    if(n == 0) {
        if(config_stream_flush(&slot->stream,
                               config_dispatch_apply_line<RadioT>,
                               &line_ctx) != 0) {
            printf("[%s] CONFIG stream flush error\n", tag);
            fflush(stdout);
        }

        client_slot_close(slot);
        return;
    }

    if(config_stream_feed(&slot->stream, buf, (size_t)n,
                          config_dispatch_apply_line<RadioT>,
                          &line_ctx) != 0) {
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
                                    RadioT& radio,
                                    volatile RadioHealth& health,
                                    const char *tag,
                                    const char *prefix,
                                    volatile RadioMode_t& mode,
                                    volatile bool& getrssi_active,
                                    ConfigApplyFn<RadioT> apply_config,
                                    void (*rx_callback)(void))
{
    for(int i=0;i<max_clients;i++){
        config_dispatch_client<RadioT>(slots, i, readfds, buf,
                                       radio, health, tag, prefix,
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
    config_dispatch_clients<RadioT>(ctx->slots,
                                    max_clients, readfds, buf,
                                    *ctx->radio, *ctx->health, ctx->tag, ctx->prefix,
                                    *ctx->mode, *ctx->getrssi_active,
                                    ctx->apply_config, ctx->rx_callback);
}

#endif
