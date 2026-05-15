#include "bridge.h"

#include "ax25.h"
#include "kiss.h"
#include "loraham_sock.h"
#include "tcp_server.h"
#include "tnc2.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

/*
 * Bridge logic.
 * One KISS/TCP client is bridged to one LoRaHAM daemon data socket.
 */

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

        printf("[LoRaHAM] TX packet len=%zu\n", lora_len);

        if (loraham_sock_write(data_fd, lora_pkt, lora_len) < 0) {
            if (stats) {
                stats->loraham_drop++;
            }

            printf("[LoRaHAM] TX write failed\n");
            return LHKT_ERR;
        }

        if (stats) {
            stats->loraham_tx++;
        }
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

static int handle_loraham_rx_chunk(int client_fd,
                                   loraham_rx_state_t *rx_state,
                                   const uint8_t *buf,
                                   size_t len,
                                   lhkt_stats_t *stats)
{
    char tnc2[LHKT_TNC2_MAX_LINE];
    size_t tnc2_len;
    int ret;

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

        return send_tnc2_to_kiss_client(client_fd, tnc2, stats);
    }

    if (ret < 0) {
        if (stats) {
            stats->loraham_drop++;
        }

        printf("[LoRaHAM] RX drop: err=%d\n", ret);
        return ret;
    }

    /*
     * The current daemon commonly writes raw packets without newline.
     * For small Unix-socket reads this usually flushes one complete packet.
     */
    if (rx_state->seen_header) {
        ret = loraham_extract_tnc2(rx_state,
                                   NULL,
                                   0,
                                   tnc2,
                                   sizeof(tnc2),
                                   &tnc2_len);
        if (ret == 1) {
            if (stats) {
                stats->loraham_rx++;
            }

            return send_tnc2_to_kiss_client(client_fd, tnc2, stats);
        }

        if (ret < 0) {
            if (stats) {
                stats->loraham_drop++;
            }

            printf("[LoRaHAM] RX flush drop: err=%d\n", ret);
            return ret;
        }
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

    data_fd = loraham_sock_connect(cfg->data_socket);
    if (data_fd < 0) {
        fprintf(stderr, "[ERR] LoRaHAM data socket connect failed: %s\n",
                cfg->data_socket);
        return 1;
    }

    printf("[LoRaHAM] Data socket connected: %s\n", cfg->data_socket);

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
    printf("[KISS] Waiting for one client...\n");

    client_fd = lhkt_tcp_server_accept(listen_fd, peer, sizeof(peer));
    if (client_fd < 0) {
        fprintf(stderr, "[ERR] KISS/TCP accept failed\n");
        lhkt_tcp_server_close(listen_fd);
        lhkt_tcp_server_close(data_fd);
        return 1;
    }

    printf("[KISS] Client connected: %s\n", peer);

    for (;;) {
        FD_ZERO(&rfds);
        FD_SET(client_fd, &rfds);
        FD_SET(data_fd, &rfds);

        max_fd = client_fd > data_fd ? client_fd : data_fd;

        ret = select(max_fd + 1, &rfds, NULL, NULL, NULL);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }

            fprintf(stderr, "[ERR] select failed\n");
            break;
        }

        if (FD_ISSET(client_fd, &rfds)) {
            n = read(client_fd, buf, sizeof(buf));
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }

                fprintf(stderr, "[ERR] KISS/TCP read failed\n");
                break;
            }

            if (n == 0) {
                printf("[KISS] Client disconnected\n");
                if (stats) {
                    stats->client_disconnects++;
                }
                break;
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

                    kiss_handle_command(&kiss_params, &kiss_frame);

                    if (kiss_frame.command == KISS_CMD_DATA) {
                        printf("[KISS] Data frame: port=%u len=%zu\n",
                               kiss_frame.port,
                               kiss_frame.data_len);
                        handle_kiss_data_frame(&kiss_frame, cfg, stats, data_fd);
                    } else if (cfg->verbose) {
                        printf("[KISS] Command: port=%u cmd=%u len=%zu\n",
                               kiss_frame.port,
                               kiss_frame.command,
                               kiss_frame.data_len);
                    }
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

        if (FD_ISSET(data_fd, &rfds)) {
            n = loraham_sock_read(data_fd, buf, sizeof(buf));
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }

                fprintf(stderr, "[ERR] LoRaHAM data socket read failed\n");
                break;
            }

            if (n == 0) {
                fprintf(stderr, "[ERR] LoRaHAM data socket closed\n");
                if (stats) {
                    stats->socket_reconnects++;
                }
                break;
            }

            if (cfg->verbose) {
                printf("[LoRaHAM] %zd bytes received\n", n);
            }

            handle_loraham_rx_chunk(client_fd, &lora_rx, buf, (size_t)n, stats);
        }
    }

    lhkt_tcp_server_close(client_fd);
    lhkt_tcp_server_close(listen_fd);
    lhkt_tcp_server_close(data_fd);

    return 0;
}
