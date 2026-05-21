#include "bridge.h"

#include "ax25.h"
#include "kiss.h"
#include "loraham_sock.h"
#include "tcp_server.h"
#include "tnc2.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <signal.h>
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

#define LHKT_RX_IDLE_FLUSH_USEC 250000
#define LHKT_RECONNECT_DELAY_SEC 1
#define LHKT_CLIENT_WRITE_TIMEOUT_SEC 2
#define LHKT_TX_RESTORE_RETRY_DELAY_MS 100
#define LHKT_TEST_HOOK_MAX_CALLS 16

/* ---- Shutdown ---- */

static volatile sig_atomic_t lhkt_bridge_stop_requested;

static void bridge_signal_handler(int signo)
{
    (void)signo;
    lhkt_bridge_stop_requested = 1;
}

static void bridge_reset_stop_requested(void)
{
    lhkt_bridge_stop_requested = 0;
}

static int bridge_should_stop(void)
{
    return lhkt_bridge_stop_requested != 0;
}

static void bridge_install_signal_handlers(void)
{
    if (signal(SIGINT, bridge_signal_handler) == SIG_ERR) {
        fprintf(stderr, "[WARN] Could not install SIGINT handler\n");
    }

    if (signal(SIGTERM, bridge_signal_handler) == SIG_ERR) {
        fprintf(stderr, "[WARN] Could not install SIGTERM handler\n");
    }
}

#ifdef LHKT_TEST
void lhkt_test_bridge_reset_stop(void)
{
    bridge_reset_stop_requested();
}

void lhkt_test_bridge_request_stop(void)
{
    bridge_signal_handler(SIGTERM);
}

int lhkt_test_bridge_should_stop(void)
{
    return bridge_should_stop();
}
#endif

/* ---- fd helpers ---- */

static int wait_fd_writable(int fd)
{
    fd_set wfds;
    struct timeval tv;
    int ret;

    if (fd < 0 || fd >= FD_SETSIZE) {
        return LHKT_ERR;
    }

    for (;;) {
        if (bridge_should_stop()) {
            return LHKT_ERR;
        }

        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);

        tv.tv_sec = LHKT_CLIENT_WRITE_TIMEOUT_SEC;
        tv.tv_usec = 0;

        ret = select(fd + 1, NULL, &wfds, NULL, &tv);
        if (ret < 0) {
            if (errno == EINTR) {
                if (bridge_should_stop()) {
                    return LHKT_ERR;
                }

                continue;
            }

            return LHKT_ERR;
        }

        if (ret == 0) {
            errno = ETIMEDOUT;
            return LHKT_ERR;
        }

        return LHKT_OK;
    }
}


#ifdef LHKT_TEST
int lhkt_test_bridge_wait_fd_writable(int fd)
{
    return wait_fd_writable(fd);
}
#endif

static int add_fd_to_set(int fd, fd_set *set, int *max_fd)
{
    if (fd < 0 || !set || !max_fd) {
        return LHKT_ERR;
    }

    if (fd >= FD_SETSIZE) {
        return LHKT_ERR_LONG;
    }

    FD_SET(fd, set);

    if (fd > *max_fd) {
        *max_fd = fd;
    }

    return LHKT_OK;
}

static int write_all_fd(int fd, const uint8_t *buf, size_t len)
{
    size_t done;
    ssize_t n;

    if (fd < 0 || fd >= FD_SETSIZE || (!buf && len > 0)) {
        return LHKT_ERR;
    }

    done = 0;

    while (done < len) {
        n = write(fd, buf + done, len - done);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (wait_fd_writable(fd) != LHKT_OK) {
                    return LHKT_ERR;
                }

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

#ifndef LHKT_TEST
static int sleep_ms(int ms)
{
    struct timeval tv;

    if (ms <= 0) {
        return LHKT_OK;
    }

    if (bridge_should_stop()) {
        return LHKT_ERR;
    }

    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;

    while (select(0, NULL, NULL, NULL, &tv) < 0 && errno == EINTR) {
        if (bridge_should_stop()) {
            return LHKT_ERR;
        }

        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
    }

    if (bridge_should_stop()) {
        return LHKT_ERR;
    }

    return LHKT_OK;
}
#endif

/* ---- Test hooks ---- */

#ifdef LHKT_TEST
static int lhkt_test_config_results[LHKT_TEST_HOOK_MAX_CALLS];
static size_t lhkt_test_config_result_count;
static size_t lhkt_test_config_result_pos;
static double lhkt_test_config_freqs[LHKT_TEST_HOOK_MAX_CALLS];
static size_t lhkt_test_config_call_count;
static int lhkt_test_write_enabled;
static ssize_t lhkt_test_write_result;
static size_t lhkt_test_write_call_count;
static size_t lhkt_test_sleep_call_count;

void lhkt_test_bridge_reset_tx_hooks(void)
{
    memset(lhkt_test_config_results, 0, sizeof(lhkt_test_config_results));
    memset(lhkt_test_config_freqs, 0, sizeof(lhkt_test_config_freqs));
    lhkt_test_config_result_count = 0;
    lhkt_test_config_result_pos = 0;
    lhkt_test_config_call_count = 0;
    lhkt_test_write_enabled = 0;
    lhkt_test_write_result = 0;
    lhkt_test_write_call_count = 0;
    lhkt_test_sleep_call_count = 0;
}

void lhkt_test_bridge_set_config_results(const int *results, size_t count)
{
    size_t i;

    lhkt_test_config_result_count = 0;
    lhkt_test_config_result_pos = 0;

    if (!results) {
        return;
    }

    if (count > LHKT_TEST_HOOK_MAX_CALLS) {
        count = LHKT_TEST_HOOK_MAX_CALLS;
    }

    for (i = 0; i < count; i++) {
        lhkt_test_config_results[i] = results[i];
    }

    lhkt_test_config_result_count = count;
}

size_t lhkt_test_bridge_config_call_count(void)
{
    return lhkt_test_config_call_count;
}

double lhkt_test_bridge_config_freq_at(size_t index)
{
    if (index >= lhkt_test_config_call_count ||
        index >= LHKT_TEST_HOOK_MAX_CALLS) {
        return 0.0;
    }

    return lhkt_test_config_freqs[index];
}

void lhkt_test_bridge_set_write_result(ssize_t result)
{
    lhkt_test_write_enabled = 1;
    lhkt_test_write_result = result;
}

size_t lhkt_test_bridge_write_call_count(void)
{
    return lhkt_test_write_call_count;
}

size_t lhkt_test_bridge_sleep_call_count(void)
{
    return lhkt_test_sleep_call_count;
}
#endif

/* ---- Timing ---- */

static int bridge_sleep_ms(int ms)
{
#ifdef LHKT_TEST
    if (ms > 0) {
        lhkt_test_sleep_call_count++;
    }

    if (bridge_should_stop()) {
        return LHKT_ERR;
    }

    return LHKT_OK;
#else
    return sleep_ms(ms);
#endif
}

#ifdef LHKT_TEST
int lhkt_test_bridge_sleep_ms(int ms)
{
    return bridge_sleep_ms(ms);
}
#endif

/* ---- LoRaHAM sockets/config ---- */

static int send_loraham_config_freq(const lhkt_config_t *cfg, double freq)
{
    int conf_fd;
    int ret;

    if (!cfg || freq <= 0.0) {
        return LHKT_ERR_FORMAT;
    }

#ifdef LHKT_TEST
    if (lhkt_test_config_call_count < LHKT_TEST_HOOK_MAX_CALLS) {
        lhkt_test_config_freqs[lhkt_test_config_call_count] = freq;
    }
    lhkt_test_config_call_count++;

    if (lhkt_test_config_result_pos < lhkt_test_config_result_count) {
        return lhkt_test_config_results[lhkt_test_config_result_pos++];
    }

    if (lhkt_test_config_result_count > 0) {
        return LHKT_OK;
    }
#endif

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


static ssize_t bridge_loraham_write(int fd,
                                    const uint8_t *buf,
                                    size_t len)
{
#ifdef LHKT_TEST
    lhkt_test_write_call_count++;
    if (lhkt_test_write_enabled) {
        return lhkt_test_write_result;
    }
#endif

    return loraham_sock_write(fd, buf, len);
}

static int restore_loraham_rx_freq(const lhkt_config_t *cfg,
                                   lhkt_stats_t *stats)
{
    int ret;

    if (!cfg || !cfg->have_rx_freq) {
        return LHKT_ERR_FORMAT;
    }

    ret = send_loraham_config_freq(cfg, cfg->rx_freq);
    if (ret == LHKT_OK) {
        return LHKT_OK;
    }

    if (stats) {
        stats->tx_restore_failures++;
    }

    printf("[LoRaHAM] RX freq restore failed, retrying\n");
    if (bridge_sleep_ms(LHKT_TX_RESTORE_RETRY_DELAY_MS) != LHKT_OK) {
        return ret;
    }

    ret = send_loraham_config_freq(cfg, cfg->rx_freq);
    if (ret == LHKT_OK) {
        printf("[LoRaHAM] RX freq restore retry OK\n");
        return LHKT_OK;
    }

    if (stats) {
        stats->tx_restore_failures++;
    }

    printf("[LoRaHAM] RX freq restore retry failed\n");
    return ret;
}

/* ---- Disconnect policy ---- */

static int should_reconnect_loraham_data_socket(int ret)
{
    return ret == LHKT_ERR_TX_SOCKET;
}


static int should_disconnect_kiss_client(int ret)
{
    return ret == LHKT_ERR_CLIENT_SOCKET;
}

static void disconnect_loraham_data_socket(int *data_fd,
                                           loraham_rx_state_t *rx_state,
                                           lhkt_stats_t *stats,
                                           const char *reason)
{
    if (!data_fd || *data_fd < 0) {
        return;
    }

    fprintf(stderr,
            "[WARN] LoRaHAM data socket %s, reconnecting\n",
            reason ? reason : "disconnected");

    lhkt_tcp_server_close(*data_fd);
    *data_fd = -1;

    if (rx_state) {
        loraham_rx_state_init(rx_state);
    }

    if (stats) {
        stats->socket_reconnects++;
    }
}

#ifdef LHKT_TEST
int lhkt_test_bridge_should_reconnect_data_socket(int ret)
{
    return should_reconnect_loraham_data_socket(ret);
}


int lhkt_test_bridge_should_disconnect_kiss_client(int ret)
{
    return should_disconnect_kiss_client(ret);
}

void lhkt_test_bridge_disconnect_data_socket(int *data_fd,
                                             loraham_rx_state_t *rx_state,
                                             lhkt_stats_t *stats)
{
    disconnect_loraham_data_socket(data_fd, rx_state, stats, "test");
}
#endif

/* ---- Periodic stats ---- */

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

/* ---- TX path: KISS -> LoRaHAM ---- */

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

        if (bridge_sleep_ms(cfg->tx_settle_ms) != LHKT_OK) {
            ret = restore_loraham_rx_freq(cfg, stats);
            if (ret != LHKT_OK) {
                printf("[LoRaHAM] Shutdown during TX settle and RX restore failed\n");
                return ret;
            }

            return LHKT_ERR;
        }

        printf("[LoRaHAM] TX packet len=%zu\n", lora_len);

        if (bridge_loraham_write(data_fd, lora_pkt, lora_len) < 0) {
            if (stats) {
                stats->loraham_drop++;
            }

            printf("[LoRaHAM] TX write failed\n");
            (void)bridge_sleep_ms(cfg->tx_return_ms);
            ret = restore_loraham_rx_freq(cfg, stats);
            if (ret != LHKT_OK) {
                printf("[LoRaHAM] TX write failed and RX restore failed\n");
            }
            return LHKT_ERR_TX_SOCKET;
        }

        (void)bridge_sleep_ms(cfg->tx_return_ms);

        ret = restore_loraham_rx_freq(cfg, stats);
        if (ret != LHKT_OK) {
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

/* ---- RX path: LoRaHAM -> KISS ---- */

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
        if (stats) {
            stats->kiss_drop++;
        }

        printf("[KISS] Client write failed\n");
        return LHKT_ERR_CLIENT_SOCKET;
    }

    if (stats) {
        stats->kiss_tx++;
    }

    printf("[KISS] Sent RX frame to client len=%zu\n", kiss_len);

    return LHKT_OK;
}

/* ---- Unit-test entry points ---- */

#ifdef LHKT_TEST
int lhkt_test_add_fd_to_set(int fd)
{
    fd_set rfds;
    int max_fd;

    FD_ZERO(&rfds);
    max_fd = -1;

    return add_fd_to_set(fd, &rfds, &max_fd);
}

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

/* ---- Main loop ---- */

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

    bridge_reset_stop_requested();
    bridge_install_signal_handlers();

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

    while (!bridge_should_stop()) {
        print_stats_if_due(cfg, stats, &next_stats);

        FD_ZERO(&rfds);
        max_fd = -1;

        if (data_fd >= 0) {
            ret = add_fd_to_set(data_fd, &rfds, &max_fd);
            if (ret != LHKT_OK) {
                fprintf(stderr, "[ERR] LoRaHAM data fd too large for select\n");
                break;
            }
        }

        if (client_fd >= 0) {
            ret = add_fd_to_set(client_fd, &rfds, &max_fd);
            if (ret != LHKT_OK) {
                fprintf(stderr, "[ERR] KISS/TCP client fd too large for select\n");
                break;
            }
        } else {
            ret = add_fd_to_set(listen_fd, &rfds, &max_fd);
            if (ret != LHKT_OK) {
                fprintf(stderr, "[ERR] KISS/TCP listen fd too large for select\n");
                break;
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
            if (should_disconnect_kiss_client(ret)) {
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

                    ret = handle_kiss_frame(&kiss_frame,
                                            &kiss_params,
                                            cfg,
                                            stats,
                                            data_fd);
                    if (should_reconnect_loraham_data_socket(ret)) {
                        disconnect_loraham_data_socket(&data_fd,
                                                       &lora_rx,
                                                       stats,
                                                       "TX write failed");
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

        if (data_fd >= 0 && FD_ISSET(data_fd, &rfds)) {
            n = loraham_sock_read(data_fd, buf, sizeof(buf));
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }

                disconnect_loraham_data_socket(&data_fd,
                                               &lora_rx,
                                               stats,
                                               "read failed");
                continue;
            }

            if (n == 0) {
                disconnect_loraham_data_socket(&data_fd,
                                               &lora_rx,
                                               stats,
                                               "closed");
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
                if (should_disconnect_kiss_client(ret)) {
                    printf("[KISS] Client write failed, disconnecting\n");
                    lhkt_tcp_server_close(client_fd);
                    client_fd = -1;
                    kiss_decoder_init(&kiss_dec);
                    kiss_params_init(&kiss_params);
                    loraham_rx_state_init(&lora_rx);
                    printf("[KISS] Waiting for client...\n");
                }
            }
        }
    }

    if (bridge_should_stop()) {
        printf("[Bridge] Shutdown requested\n");
    }

    if (client_fd >= 0) {
        lhkt_tcp_server_close(client_fd);
    }
    lhkt_tcp_server_close(listen_fd);
    lhkt_tcp_server_close(data_fd);

    return 0;
}
