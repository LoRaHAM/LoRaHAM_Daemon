#ifndef LORAHAM_KISS_TNC_H
#define LORAHAM_KISS_TNC_H

#include <stddef.h>
#include <stdint.h>

/* Gemeinsame Limits */
#define LHKT_AX25_MAX_REPEATERS 8
#define LHKT_AX25_MAX_ADDRS     10
#define LHKT_AX25_MAX_PAYLOAD   256
#define LHKT_AX25_MAX_FRAME     384
#define LHKT_TNC2_MAX_LINE      512
#define LHKT_KISS_MAX_FRAME     1024
#define LHKT_LORAHAM_TX_MAX     255
#define LHKT_LORAHAM_HDR_LEN    3
#define LHKT_PATH_MAX           256
#define LHKT_HOST_MAX           64

/* Defaults */
#define LHKT_DEFAULT_KISS_HOST   "127.0.0.1"
#define LHKT_DEFAULT_KISS_PORT   8001
#define LHKT_DEFAULT_DATA_SOCKET "/tmp/lora433.sock"
#define LHKT_DEFAULT_CONF_SOCKET "/tmp/loraconf433.sock"
/* Fehlercodes */
#define LHKT_OK                 0
#define LHKT_ERR               -1
#define LHKT_ERR_SHORT         -2
#define LHKT_ERR_LONG          -3
#define LHKT_ERR_FORMAT        -4
#define LHKT_ERR_UNSUPPORTED   -5
#define LHKT_ERR_NOSPACE       -6
#define LHKT_ERR_TX_SOCKET     -7
#define LHKT_ERR_CLIENT_SOCKET -8

typedef struct {
    char kiss_host[LHKT_HOST_MAX];
    int  kiss_port;

    char data_socket[LHKT_PATH_MAX];
    char conf_socket[LHKT_PATH_MAX];

    int rx_only;
    int verbose;
    int stats_interval;
    int tx_settle_ms;
    int tx_return_ms;


    double rx_freq;
    double tx_freq;
    int have_rx_freq;
    int have_tx_freq;

    int sf;
    double bw;
    int cr;
    int crc;
    int preamble;
    int syncword;
    int ldro;
    int ldro_auto;
    int power;
} lhkt_config_t;

typedef struct {
    uint64_t kiss_rx;
    uint64_t kiss_tx;
    uint64_t kiss_drop;

    uint64_t ax25_rx;
    uint64_t ax25_tx;
    uint64_t ax25_drop;

    uint64_t tnc2_rx;
    uint64_t tnc2_tx;
    uint64_t tnc2_drop;

    uint64_t loraham_rx;
    uint64_t loraham_tx;
    uint64_t loraham_drop;

    uint64_t tx_drop_oversize;
    uint64_t tx_restore_failures;
    uint64_t socket_reconnects;
    uint64_t client_disconnects;
} lhkt_stats_t;

void lhkt_config_defaults(lhkt_config_t *cfg);
void lhkt_stats_init(lhkt_stats_t *stats);
void lhkt_stats_print(const lhkt_stats_t *stats);

#endif
