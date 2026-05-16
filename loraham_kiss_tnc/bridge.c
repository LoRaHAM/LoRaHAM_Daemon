#include "bridge.h"

#include "ax25.h"
#include "kiss.h"
#include "loraham_sock.h"
#include "tcp_server.h"
#include "tnc2.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

/*
 * Bridge logic.
 * One KISS/TCP client is bridged to one LoRaHAM daemon data socket.
 */

#define LHKT_RX_IDLE_FLUSH_USEC 50000
#define LHKT_RECONNECT_DELAY_SEC 1

static int write_all_fd(int fd, const uint8_t *buf, size_t len)
{
    size_t done;
    ssize_t n;

    if (fd < 0 || !buf) {
        return LHKT_ERR;
    }

    done = 0;

    while (done < len) {
        n = write(fd, buf + done, len - done);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }

            return LHKT_ERR;
        }

        if (n == 0) {
            return LHKT_ERR;
        }

        done += (size_t)n;
    }

    return LHKT_OK;
}

static int set_fd_nonblocking(int fd)
{
    int flags;

    if (fd < 0) {
        return LHKT_ERR;
    }

    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return LHKT_ERR;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return LHKT_ERR;
    }

    return LHKT_OK;
}

static void sleep_ms(int ms)
{
    struct timeval tv;

    if (ms <= 0) {
        return;
    }

    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;

    while (select(0, NULL, NULL, NULL, &tv) < 0 && errno == EINTR) {
        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
    }
}

static int send_loraham_config_freq(const lhkt_config_t *cfg, double freq)
{
    int conf_fd;
    int ret;

    if (!cfg || freq <= 0.0) {
        return LHKT_ERR_FORMAT;
    }

    conf_fd = loraham_sock_connect(cfg->conf_socket);
    if (conf_fd < 0) {
        printf("[LoRaHAM] Config socket not connected: %s\n", cfg->conf_socket);
        return LHKT_ERR;
    }

    ret = loraham_send_config_freq(conf_fd, cfg, freq);
    lhkt_tcp_server_close(conf_fd);

    if (ret != LHKT_OK) {
        printf("[LoRaHAM] Config send failed: freq=%.6f\n", freq);
        return ret;
    }

    printf("[LoRaHAM] Config sent: freq=%.6f\n", freq);
    return LHKT_OK;
}

static int connect_loraham_data_socket(const lhkt_config_t *cfg)
{
    int fd;

    if (!cfg) {
        return LHKT_ERR;
    }

    fd = loraham_sock_connect(cfg->data_socket);
    if (fd < 0) {
        return LHKT_ERR;
    }

    printf("[LoRaHAM] Data socket connected: %s\n", cfg->data_socket);
    return fd;
}

static void print_stats_if_due(const lhkt_config_t *cfg,
                               lhkt_stats_t *stats,
                               time_t *next_stats)
{
    time_t now;

    if (!cfg || !stats || !next_stats || cfg->stats_interval <= 0) {
        return;
    }

    now = time(NULL);
    if (*next_stats == 0) {
        *next_stats = now + cfg->stats_interval;
        return;
    }

    if (now < *next_stats) {
        return;
    }

    lhkt_stats_print(stats);
    *next_stats = now + cfg->stats_interval;
}

/*
 * TX path:
 * KISS data frame -> AX.25 UI -> TNC2 -> LoRaHAM packet -> daemon socket.
 */
static int handle_kiss_data_frame(const kiss_frame_t *kiss_frame,
                                  const lhkt_config_t *cfg,
                                  lhkt_stats_t *stats,
                                  int data_fd)
{
    ax25_frame_t ax25;
    char tnc2[LHKT_TNC2_MAX_LINE];
    size_t tnc2_len;
    int ret;

    if (!kiss_frame || !cfg) {
        return LHKT_ERR;
    }

    ret = ax25_decode_ui(kiss_frame->data, kiss_frame->data_len, &ax25);
    if (ret != LHKT_OK) {
        if (stats) {
            stats->ax25_drop++;
        }

        printf("[AX25] Drop invalid/non-UI frame: err=%d len=%zu\n",
               ret,
               kiss_frame->data_len);
        return ret;
    }

    if (stats) {
        stats->ax25_rx++;
    }

    ret = tnc2_format_line(&ax25, tnc2, sizeof(tnc2), &tnc2_len);
    if (ret != LHKT_OK) {
        if (stats) {
            stats->tnc2_drop++;
        }

        printf("[TNC2] Drop frame, format failed: err=%d\n", ret);
        return ret;
    }

    if (stats) {
        stats->tnc2_tx++;
    }

    printf("[TNC2 TX] %s\n", tnc2);

    {
        uint8_t lora_pkt[LHKT_LORAHAM_TX_MAX];
        size_t lora_len = 0;

        ret = loraham_build_aprs_packet(tnc2,
                                        lora_pkt,
                                        sizeof(lora_pkt),
                                        &lora_len);
        if (ret != LHKT_OK) {
            if (stats) {
                stats->loraham_drop++;

                if (ret == LHKT_ERR_LONG) {
                    stats->tx_drop_oversize++;
                }
            }

            printf("[LoRaHAM] TX drop: err=%d tnc2_len=%zu\n",
                   ret,
                   tnc2_len);
            return ret;
        }

        if (cfg->rx_only) {
            printf("[LoRaHAM] RX-only: TX suppressed packet len=%zu\n", lora_len);
            return LHKT_OK;
        }

        if (data_fd < 0) {
            if (stats) {
                stats->loraham_drop++;
            }

            printf("[LoRaHAM] TX drop: data socket not connected\n");
            return LHKT_ERR;
        }

        if (!cfg->have_tx_freq || !cfg->have_rx_freq) {
            if (stats) {
                stats->loraham_drop++;
            }

            printf("[LoRaHAM] TX drop: rx_freq and tx_freq required\n");
            return LHKT_ERR_FORMAT;
        }

        ret = send_loraham_config_freq(cfg, cfg->tx_freq);
        if (ret != LHKT_OK) {
            if (stats) {
                stats->loraham_drop++;
            }

            printf("[LoRaHAM] TX drop: switch to tx_freq failed\n");
            return ret;
        }

        sleep_ms(cfg->tx_settle_ms);

        printf("[LoRaHAM] TX packet len=%zu\n", lora_len);

        if (loraham_sock_write(data_fd, lora_pkt, lora_len) < 0) {
            if (stats) {
                stats->loraham_drop++;
            }

            printf("[LoRaHAM] TX write failed\n");
            sleep_ms(cfg->tx_return_ms);
            send_loraham_config_freq(cfg, cfg->rx_freq);
            return LHKT_ERR;
        }

        sleep_ms(cfg->tx_return_ms);

        ret = send_loraham_config_freq(cfg, cfg->rx_freq);
        if (ret != LHKT_OK) {
            printf("[LoRaHAM] RX freq restore failed\n");
            return ret;
        }

        if (stats) {
            stats->loraham_tx++;
        }
    }

    return LHKT_OK;
}

static int handle_kiss_frame(const kiss_frame_t *kiss_frame,
                             kiss_params_t *kiss_params,
                             const lhkt_config_t *cfg,
                             lhkt_stats_t *stats,
                             int data_fd)
{
    int ret;

    if (!kiss_frame || !cfg) {
        return LHKT_ERR;
    }

    kiss_handle_command(kiss_params, kiss_frame);

    if (kiss_frame->command == KISS_CMD_DATA) {
        printf("[KISS] Data frame: port=%u len=%zu\n",
               kiss_frame->port,
               kiss_frame->data_len);

        if (kiss_frame->port != 0) {
            if (stats) {
                stats->kiss_drop++;
            }

            printf("[KISS] Drop unsupported port: %u\n",
                   kiss_frame->port);
            return LHKT_ERR_UNSUPPORTED;
        }

        ret = handle_kiss_data_frame(kiss_frame, cfg, stats, data_fd);
        return ret;
    }

    if (cfg->verbose) {
        printf("[KISS] Command: port=%u cmd=%u len=%zu\n",
               kiss_frame->port,
               kiss_frame->command,
               kiss_frame->data_len);
    }

    return LHKT_OK;
}

/*
 * RX path:
 * TNC2 from LoRaHAM daemon -> AX.25 UI -> KISS data frame -> TCP client.
 */
static int send_tnc2_to_kiss_client(int client_fd,
                                    const char *tnc2,
                                    lhkt_stats_t *stats)
{
    ax25_frame_t ax25;
    uint8_t ax25_raw[LHKT_AX25_MAX_FRAME];
    uint8_t kiss_raw[LHKT_KISS_MAX_FRAME];
    size_t ax25_len;
    size_t kiss_len;
    int ret;

    if (client_fd < 0 || !tnc2) {
        return LHKT_ERR;
    }

    printf("[TNC2 RX] %s\n", tnc2);

    ret = tnc2_parse_line(tnc2, &ax25);
    if (ret != LHKT_OK) {
        if (stats) {
            stats->tnc2_drop++;
        }

        printf("[TNC2] RX drop, parse failed: err=%d\n", ret);
        return ret;
    }

    if (stats) {
        stats->tnc2_rx++;
    }

    ret = ax25_encode_ui(&ax25, ax25_raw, sizeof(ax25_raw), &ax25_len);
    if (ret != LHKT_OK) {
        if (stats) {
            stats->ax25_drop++;
        }

        printf("[AX25] RX drop, encode failed: err=%d\n", ret);
        return ret;
    }

    if (stats) {
        stats->ax25_tx++;
    }

    ret = kiss_encode_data_frame(ax25_raw,
                                 ax25_len,
                                 kiss_raw,
                                 sizeof(kiss_raw),
                                 &kiss_len);
    if (ret != LHKT_OK) {
        if (stats) {
            stats->kiss_drop++;
        }

        printf("[KISS] RX drop, encode failed: err=%d\n", ret);
        return ret;
    }

    if (write_all_fd(client_fd, kiss_raw, kiss_len) != LHKT_OK) {
        printf("[KISS] Client write failed\n");
        return LHKT_ERR;
    }

    if (stats) {
        stats->kiss_tx++;
    }

    printf("[KISS] Sent RX frame to client len=%zu\n", kiss_len);

    return LHKT_OK;
}

#ifdef LHKT_TEST
int lhkt_test_send_tnc2_to_kiss_client(int client_fd,
                                       const char *tnc2,
                                       lhkt_stats_t *stats)
{
    return send_tnc2_to_kiss_client(client_fd, tnc2, stats);
}

int lhkt_test_handle_kiss_frame(const kiss_frame_t *kiss_frame,
                                kiss_params_t *kiss_params,
                                const lhkt_config_t *cfg,
                                lhkt_stats_t *stats,
                                int data_fd)
{
    return handle_kiss_frame(kiss_frame,
                             kiss_params,
                             cfg,
                             stats,
                             data_fd);
}
#endif

static int handle_loraham_rx_chunk(int client_fd,
                                   loraham_rx_state_t *rx_state,
                                   const uint8_t *buf,
                                   size_t len,
                                   lhkt_stats_t *stats)
{
    char tnc2[LHKT_TNC2_MAX_LINE];
    size_t tnc2_len;
    int ret;

    for (;;) {
        ret = loraham_extract_tnc2(rx_state,
                                   buf,
                                   len,
                                   tnc2,
                                   sizeof(tnc2),
                                   &tnc2_len);
        if (ret == 1) {
            if (stats) {
                stats->loraham_rx++;
            }

            ret = send_tnc2_to_kiss_client(client_fd, tnc2, stats);
            if (ret != LHKT_OK) {
                return ret;
            }
        }

        if (ret < 0) {
            if (stats) {
                stats->loraham_drop++;
            }

            printf("[LoRaHAM] RX drop: err=%d\n", ret);
            return ret;
        }

        /* Drain queued complete packets without forcing tail flush. */
        if (rx_state->pending_len == 0) {
            break;
        }

        buf = NULL;
        len = 0;
    }

    return LHKT_OK;
}

int lhkt_bridge_run(const lhkt_config_t *cfg, lhkt_stats_t *stats)
{
    int listen_fd;
    int client_fd;
    int data_fd;
    int conf_fd;
    char peer[64];
    uint8_t buf[512];
    ssize_t n;
    size_t i;
    int ret;
    int max_fd;
    fd_set rfds;
    struct timeval tv;
    struct timeval *timeout;
    time_t next_stats;
    time_t now;
    long stats_wait;

    kiss_decoder_t kiss_dec;
    kiss_frame_t kiss_frame;
    kiss_params_t kiss_params;
    loraham_rx_state_t lora_rx;

    if (!cfg) {
        return 1;
    }

    data_fd = -1;
    conf_fd = -1;
    listen_fd = -1;
    client_fd = -1;
    next_stats = 0;

    kiss_decoder_init(&kiss_dec);
    kiss_params_init(&kiss_params);
    loraham_rx_state_init(&lora_rx);

    conf_fd = loraham_sock_connect(cfg->conf_socket);
    if (conf_fd >= 0) {
        if (loraham_send_config(conf_fd, cfg) == LHKT_OK) {
            printf("[LoRaHAM] Config sent: %s\n", cfg->conf_socket);
        } else {
            printf("[LoRaHAM] Config send failed: %s\n", cfg->conf_socket);
        }

        lhkt_tcp_server_close(conf_fd);
    } else {
        printf("[LoRaHAM] Config socket not connected: %s\n", cfg->conf_socket);
    }

    data_fd = connect_loraham_data_socket(cfg);
    if (data_fd < 0) {
        fprintf(stderr,
                "[WARN] LoRaHAM data socket not connected yet: %s\n",
                cfg->data_socket);
    }

    if (cfg->rx_only) {
        printf("[LoRaHAM] RX-only mode: TX disabled\n");
    }

    listen_fd = lhkt_tcp_server_listen(cfg->kiss_host, cfg->kiss_port);
    if (listen_fd < 0) {
        fprintf(stderr, "[ERR] KISS/TCP listen failed on %s:%d\n",
                cfg->kiss_host,
                cfg->kiss_port);
        lhkt_tcp_server_close(data_fd);
        return 1;
    }

    printf("[KISS] Listening on %s:%d\n", cfg->kiss_host, cfg->kiss_port);
    printf("[KISS] Waiting for client...\n");

    for (;;) {
        print_stats_if_due(cfg, stats, &next_stats);

        FD_ZERO(&rfds);
        max_fd = -1;

        if (data_fd >= 0) {
            FD_SET(data_fd, &rfds);
            max_fd = data_fd;
        }

        if (client_fd >= 0) {
            FD_SET(client_fd, &rfds);
            if (client_fd > max_fd) {
                max_fd = client_fd;
            }
        } else {
            FD_SET(listen_fd, &rfds);
            if (listen_fd > max_fd) {
                max_fd = listen_fd;
            }
        }

        timeout = NULL;
        if (data_fd < 0) {
            tv.tv_sec = LHKT_RECONNECT_DELAY_SEC;
            tv.tv_usec = 0;
            timeout = &tv;
        } else if (client_fd >= 0 && lora_rx.seen_header && lora_rx.len > 0) {
            tv.tv_sec = 0;
            tv.tv_usec = LHKT_RX_IDLE_FLUSH_USEC;
            timeout = &tv;
        }

        if (stats && cfg->stats_interval > 0) {
            now = time(NULL);
            if (next_stats == 0) {
                next_stats = now + cfg->stats_interval;
            }

            if (next_stats <= now) {
                tv.tv_sec = 0;
                tv.tv_usec = 0;
                timeout = &tv;
            } else {
                stats_wait = (long)(next_stats - now);
                if (!timeout ||
                    tv.tv_sec > stats_wait ||
                    (tv.tv_sec == stats_wait && tv.tv_usec > 0)) {
                    tv.tv_sec = stats_wait;
                    tv.tv_usec = 0;
                    timeout = &tv;
                }
            }
        }

        ret = select(max_fd + 1, &rfds, NULL, NULL, timeout);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }

            fprintf(stderr, "[ERR] select failed\n");
            break;
        }

        if (ret == 0) {
            if (data_fd < 0) {
                data_fd = connect_loraham_data_socket(cfg);
                if (data_fd >= 0 && stats) {
                    stats->socket_reconnects++;
                }
                continue;
            }

            ret = handle_loraham_rx_chunk(client_fd,
                                          &lora_rx,
                                          NULL,
                                          0,
                                          stats);
            if (ret == LHKT_ERR) {
                printf("[KISS] Client write failed, disconnecting\n");
                lhkt_tcp_server_close(client_fd);
                client_fd = -1;
                kiss_decoder_init(&kiss_dec);
                kiss_params_init(&kiss_params);
                loraham_rx_state_init(&lora_rx);
                printf("[KISS] Waiting for client...\n");
            }
            continue;
        }

        if (client_fd < 0 && FD_ISSET(listen_fd, &rfds)) {
            client_fd = lhkt_tcp_server_accept(listen_fd, peer, sizeof(peer));
            if (client_fd < 0) {
                if (errno == EINTR) {
                    continue;
                }

                fprintf(stderr, "[ERR] KISS/TCP accept failed\n");
                continue;
            }

            if (set_fd_nonblocking(client_fd) != LHKT_OK) {
                fprintf(stderr, "[ERR] KISS/TCP non-blocking setup failed\n");
                lhkt_tcp_server_close(client_fd);
                client_fd = -1;
                continue;
            }

            kiss_decoder_init(&kiss_dec);
            kiss_params_init(&kiss_params);
            loraham_rx_state_init(&lora_rx);

            printf("[KISS] Client connected: %s\n", peer);
        }

        if (client_fd >= 0 && FD_ISSET(client_fd, &rfds)) {
            n = read(client_fd, buf, sizeof(buf));
            if (n < 0) {
                if (errno == EINTR ||
                    errno == EAGAIN ||
                    errno == EWOULDBLOCK) {
                    continue;
                }

                fprintf(stderr, "[ERR] KISS/TCP read failed\n");
                lhkt_tcp_server_close(client_fd);
                client_fd = -1;
                kiss_decoder_init(&kiss_dec);
                kiss_params_init(&kiss_params);
                loraham_rx_state_init(&lora_rx);
                printf("[KISS] Waiting for client...\n");
                continue;
            }

            if (n == 0) {
                printf("[KISS] Client disconnected\n");
                if (stats) {
                    stats->client_disconnects++;
                }

                lhkt_tcp_server_close(client_fd);
                client_fd = -1;
                kiss_decoder_init(&kiss_dec);
                kiss_params_init(&kiss_params);
                loraham_rx_state_init(&lora_rx);
                printf("[KISS] Waiting for client...\n");
                continue;
            }

            if (cfg->verbose) {
                printf("[KISS] %zd bytes received\n", n);
            }

            for (i = 0; i < (size_t)n; i++) {
                ret = kiss_decode_byte(&kiss_dec, buf[i], &kiss_frame);

                if (ret == 1) {
                    if (stats) {
                        stats->kiss_rx++;
                    }

                    handle_kiss_frame(&kiss_frame,
                                      &kiss_params,
                                      cfg,
                                      stats,
                                      data_fd);
                } else if (ret < 0) {
                    if (stats) {
                        stats->kiss_drop++;
                    }

                    if (cfg->verbose) {
                        printf("[KISS] Dropped malformed frame: err=%d\n", ret);
                    }
                }
            }
        }

        if (data_fd >= 0 && FD_ISSET(data_fd, &rfds)) {
            n = loraham_sock_read(data_fd, buf, sizeof(buf));
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }

                fprintf(stderr, "[WARN] LoRaHAM data socket read failed, reconnecting\n");
                lhkt_tcp_server_close(data_fd);
                data_fd = -1;
                loraham_rx_state_init(&lora_rx);
                if (stats) {
                    stats->socket_reconnects++;
                }
                continue;
            }

            if (n == 0) {
                fprintf(stderr, "[WARN] LoRaHAM data socket closed, reconnecting\n");
                lhkt_tcp_server_close(data_fd);
                data_fd = -1;
                loraham_rx_state_init(&lora_rx);
                if (stats) {
                    stats->socket_reconnects++;
                }
                continue;
            }

            if (cfg->verbose) {
                printf("[LoRaHAM] %zd bytes received\n", n);
            }

            if (client_fd < 0) {
                loraham_rx_state_init(&lora_rx);
                if (stats) {
                    stats->loraham_drop++;
                }

                if (cfg->verbose) {
                    printf("[LoRaHAM] RX drop: no KISS client\n");
                }
            } else {
                ret = handle_loraham_rx_chunk(client_fd,
                                              &lora_rx,
                                              buf,
                                              (size_t)n,
                                              stats);
                if (ret == LHKT_ERR) {
                    printf("[KISS] Client write failed, disconnecting\n");
                    lhkt_tcp_server_close(client_fd);
                    client_fd = -1;
                    kiss_decoder_init(&kiss_dec);
                    kiss_params_init(&kiss_params);
                    printf("[KISS] Waiting for client...\n");
                }
            }
        }
    }

    if (client_fd >= 0) {
        lhkt_tcp_server_close(client_fd);
    }
    lhkt_tcp_server_close(listen_fd);
    lhkt_tcp_server_close(data_fd);

    return 0;
}
