#include "cli.h"
#include "config.h"

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Command line handling.
 * Order: defaults -> optional config file -> CLI overrides.
 */

void lhkt_cli_print_usage(const char *prog)
{
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("\n");
    printf("LoRaHAM KISS/TCP TNC bridge\n");
    printf("\n");
    printf("Options:\n");
    printf("  -c, --config FILE        Load config file\n");
    printf("      --kiss-host HOST     KISS/TCP bind host\n");
    printf("      --kiss-port PORT     KISS/TCP bind port\n");
    printf("      --data-socket PATH   LoRaHAM data socket\n");
    printf("      --conf-socket PATH   LoRaHAM config socket\n");
    printf("      --rx-freq MHz        RX/config frequency\n");
    printf("      --tx-freq MHz        TX/config frequency\n");
    printf("      --rx-only            Disable TX\n");
    printf("  -v, --verbose            Verbose output\n");
    printf("  -h, --help               Show help\n");
}

static int copy_arg(char *dst, size_t dst_size, const char *src)
{
    int ret;

    if (!dst || dst_size == 0 || !src) {
        return LHKT_ERR;
    }

    ret = snprintf(dst, dst_size, "%s", src);
    if (ret < 0 || (size_t)ret >= dst_size) {
        return LHKT_ERR_NOSPACE;
    }

    return LHKT_OK;
}

static int parse_int_arg(const char *text, int min, int max, int *out)
{
    char *end;
    long value;

    if (!text || !out || text[0] == '\0') {
        return LHKT_ERR_FORMAT;
    }

    errno = 0;
    value = strtol(text, &end, 0);

    if (errno != 0 || *end != '\0') {
        return LHKT_ERR_FORMAT;
    }

    if (value < min || value > max) {
        return LHKT_ERR_FORMAT;
    }

    *out = (int)value;
    return LHKT_OK;
}

static int parse_double_arg(const char *text, double min, double max, double *out)
{
    char *end;
    double value;

    if (!text || !out || text[0] == '\0') {
        return LHKT_ERR_FORMAT;
    }

    errno = 0;
    value = strtod(text, &end);

    if (errno != 0 || *end != '\0') {
        return LHKT_ERR_FORMAT;
    }

    if (value < min || value > max) {
        return LHKT_ERR_FORMAT;
    }

    *out = value;
    return LHKT_OK;
}

static const char *find_config_arg(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) {
            if (i + 1 < argc) {
                return argv[i + 1];
            }

            return NULL;
        }

        if (strncmp(argv[i], "--config=", 9) == 0) {
            return argv[i] + 9;
        }
    }

    return NULL;
}

static int parse_cli_args(int argc, char **argv, lhkt_config_t *cfg)
{
    enum {
        OPT_KISS_HOST = 1000,
        OPT_KISS_PORT,
        OPT_DATA_SOCKET,
        OPT_CONF_SOCKET,
        OPT_RX_FREQ,
        OPT_TX_FREQ,
        OPT_RX_ONLY
    };

    static const struct option long_opts[] = {
        { "config",      required_argument, 0, 'c' },
        { "kiss-host",   required_argument, 0, OPT_KISS_HOST },
        { "kiss-port",   required_argument, 0, OPT_KISS_PORT },
        { "data-socket", required_argument, 0, OPT_DATA_SOCKET },
        { "conf-socket", required_argument, 0, OPT_CONF_SOCKET },
        { "rx-freq",     required_argument, 0, OPT_RX_FREQ },
        { "tx-freq",     required_argument, 0, OPT_TX_FREQ },
        { "rx-only",     no_argument,       0, OPT_RX_ONLY },
        { "verbose",     no_argument,       0, 'v' },
        { "help",        no_argument,       0, 'h' },
        { 0, 0, 0, 0 }
    };

    int opt;
    int ret;

    if (!cfg) {
        return LHKT_ERR;
    }

    optind = 1;

    while ((opt = getopt_long(argc, argv, "c:vh", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'c':
            /* Already handled in the first pass. */
            break;

        case 'v':
            cfg->verbose = 1;
            break;

        case 'h':
            lhkt_cli_print_usage(argv[0]);
            exit(0);

        case OPT_KISS_HOST:
            ret = copy_arg(cfg->kiss_host, sizeof(cfg->kiss_host), optarg);
            if (ret != LHKT_OK) {
                return ret;
            }
            break;

        case OPT_KISS_PORT:
            ret = parse_int_arg(optarg, 1, 65535, &cfg->kiss_port);
            if (ret != LHKT_OK) {
                return ret;
            }
            break;

        case OPT_DATA_SOCKET:
            ret = copy_arg(cfg->data_socket, sizeof(cfg->data_socket), optarg);
            if (ret != LHKT_OK) {
                return ret;
            }
            break;

        case OPT_CONF_SOCKET:
            ret = copy_arg(cfg->conf_socket, sizeof(cfg->conf_socket), optarg);
            if (ret != LHKT_OK) {
                return ret;
            }
            break;

        case OPT_RX_FREQ:
            ret = parse_double_arg(optarg, 0.001, 10000.0, &cfg->rx_freq);
            if (ret != LHKT_OK) {
                return ret;
            }
            cfg->have_rx_freq = 1;
            break;

        case OPT_TX_FREQ:
            ret = parse_double_arg(optarg, 0.001, 10000.0, &cfg->tx_freq);
            if (ret != LHKT_OK) {
                return ret;
            }
            cfg->have_tx_freq = 1;
            break;

        case OPT_RX_ONLY:
            cfg->rx_only = 1;
            break;

        default:
            return LHKT_ERR_FORMAT;
        }
    }

    if (optind < argc) {
        return LHKT_ERR_FORMAT;
    }

    return LHKT_OK;
}

int lhkt_cli_apply(int argc, char **argv, lhkt_config_t *cfg)
{
    const char *config_path;
    int ret;

    if (!cfg) {
        return LHKT_ERR;
    }

    config_path = find_config_arg(argc, argv);
    if (config_path) {
        ret = lhkt_config_load_file(cfg, config_path);
        if (ret != LHKT_OK) {
            fprintf(stderr, "[ERR] Config load failed: %s\n", config_path);
            return ret;
        }

        printf("[Init] Config loaded: %s\n", config_path);
    }

    ret = parse_cli_args(argc, argv, cfg);
    if (ret != LHKT_OK) {
        fprintf(stderr, "[ERR] Invalid option or argument\n");
        lhkt_cli_print_usage(argv[0]);
        return ret;
    }

    return LHKT_OK;
}

void lhkt_cli_print_config(const lhkt_config_t *cfg)
{
    if (!cfg) {
        return;
    }

    printf("[CFG] kiss_host=%s\n", cfg->kiss_host);
    printf("[CFG] kiss_port=%d\n", cfg->kiss_port);
    printf("[CFG] data_socket=%s\n", cfg->data_socket);
    printf("[CFG] conf_socket=%s\n", cfg->conf_socket);
    printf("[CFG] mode=%s\n", cfg->mode);
    printf("[CFG] rx_only=%d\n", cfg->rx_only);
    printf("[CFG] verbose=%d\n", cfg->verbose);
    printf("[CFG] stats_interval=%d\n", cfg->stats_interval);

    if (cfg->have_rx_freq) {
        printf("[CFG] rx_freq=%.6f\n", cfg->rx_freq);
    }

    if (cfg->have_tx_freq) {
        printf("[CFG] tx_freq=%.6f\n", cfg->tx_freq);
    }

    printf("[CFG] sf=%d bw=%.3f cr=%d crc=%d preamble=%d syncword=0x%02X ldro=%d power=%d\n",
           cfg->sf,
           cfg->bw,
           cfg->cr,
           cfg->crc,
           cfg->preamble,
           cfg->syncword,
           cfg->ldro,
           cfg->power);
}
