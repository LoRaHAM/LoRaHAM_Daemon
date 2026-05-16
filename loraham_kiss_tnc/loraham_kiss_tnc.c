#include "loraham_kiss_tnc.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

void lhkt_config_defaults(lhkt_config_t *cfg)
{
    if (!cfg) {
        return;
    }

    memset(cfg, 0, sizeof(*cfg));

    snprintf(cfg->kiss_host, sizeof(cfg->kiss_host), "%s", LHKT_DEFAULT_KISS_HOST);
    cfg->kiss_port = LHKT_DEFAULT_KISS_PORT;

    snprintf(cfg->data_socket, sizeof(cfg->data_socket), "%s", LHKT_DEFAULT_DATA_SOCKET);
    snprintf(cfg->conf_socket, sizeof(cfg->conf_socket), "%s", LHKT_DEFAULT_CONF_SOCKET);

    cfg->rx_only = 0;
    cfg->verbose = 0;
    cfg->stats_interval = 60;
    cfg->tx_settle_ms = 100;
    cfg->tx_return_ms = 1000;


    cfg->rx_freq = 433.775;
    cfg->tx_freq = 433.900;
    cfg->have_rx_freq = 1;
    cfg->have_tx_freq = 1;

    cfg->sf = 12;
    cfg->bw = 125.0;
    cfg->cr = 5;
    cfg->crc = 1;
    cfg->preamble = 8;
    cfg->syncword = 0x12;
    cfg->ldro = 1;
    cfg->ldro_auto = 0;
    cfg->power = 17;
}

void lhkt_stats_init(lhkt_stats_t *stats)
{
    if (!stats) {
        return;
    }

    memset(stats, 0, sizeof(*stats));
}

void lhkt_stats_print(const lhkt_stats_t *stats)
{
    if (!stats) {
        return;
    }

    printf("[STAT] KISS rx=%" PRIu64 " tx=%" PRIu64 " drop=%" PRIu64 "\n",
           stats->kiss_rx, stats->kiss_tx, stats->kiss_drop);
    printf("[STAT] AX25 rx=%" PRIu64 " tx=%" PRIu64 " drop=%" PRIu64 "\n",
           stats->ax25_rx, stats->ax25_tx, stats->ax25_drop);
    printf("[STAT] TNC2 rx=%" PRIu64 " tx=%" PRIu64 " drop=%" PRIu64 "\n",
           stats->tnc2_rx, stats->tnc2_tx, stats->tnc2_drop);
    printf("[STAT] LoRaHAM rx=%" PRIu64 " tx=%" PRIu64 " drop=%" PRIu64 "\n",
           stats->loraham_rx, stats->loraham_tx, stats->loraham_drop);
    printf("[STAT] TX oversize=%" PRIu64 " reconnects=%" PRIu64 " client_disconnects=%" PRIu64 "\n",
           stats->tx_drop_oversize,
           stats->socket_reconnects,
           stats->client_disconnects);
}
