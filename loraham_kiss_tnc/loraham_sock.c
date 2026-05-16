#include "loraham_sock.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/*
 * LoRaHAM daemon sockets are Unix stream sockets.
 * TX writes one complete LoRaHAM APRS packet.
 * RX scans a byte stream for the LoRaHAM APRS header.
 */

void loraham_rx_state_init(loraham_rx_state_t *state)
{
    if (!state) {
        return;
    }

    memset(state, 0, sizeof(*state));
}

int loraham_sock_connect(const char *path)
{
    int fd;
    struct sockaddr_un addr;

    if (!path || path[0] == '\0') {
        return LHKT_ERR_FORMAT;
    }

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return LHKT_ERR;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;

    if (strlen(path) >= sizeof(addr.sun_path)) {
        close(fd);
        return LHKT_ERR_LONG;
    }

    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return LHKT_ERR;
    }

    return fd;
}

ssize_t loraham_sock_read(int fd,
                          uint8_t *buf,
                          size_t buf_size)
{
    if (fd < 0 || !buf || buf_size == 0) {
        return -1;
    }

    return read(fd, buf, buf_size);
}

ssize_t loraham_sock_write(int fd,
                           const uint8_t *buf,
                           size_t len)
{
    size_t done;
    ssize_t n;

    if (fd < 0 || !buf) {
        return -1;
    }

    done = 0;

    while (done < len) {
        n = write(fd, buf + done, len - done);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }

            return -1;
        }

        if (n == 0) {
            return -1;
        }

        done += (size_t)n;
    }

    return (ssize_t)done;
}

/*
 * Build one LoRaHAM APRS packet:
 *   0x3c 0xff 0x01 + TNC2 text
 *
 * The daemon/RF side must not receive more than 255 bytes.
 */
int loraham_build_aprs_packet(const char *tnc2,
                              uint8_t *out,
                              size_t out_size,
                              size_t *out_len)
{
    size_t len;

    if (!tnc2 || !out || !out_len) {
        return LHKT_ERR;
    }

    len = strlen(tnc2);
    *out_len = 0;

    if (LHKT_LORAHAM_HDR_LEN + len > LHKT_LORAHAM_TX_MAX) {
        return LHKT_ERR_LONG;
    }

    if (out_size < LHKT_LORAHAM_HDR_LEN + len) {
        return LHKT_ERR_NOSPACE;
    }

    out[0] = LORAHAM_APRS_HDR0;
    out[1] = LORAHAM_APRS_HDR1;
    out[2] = LORAHAM_APRS_HDR2;

    memcpy(out + LHKT_LORAHAM_HDR_LEN, tnc2, len);
    *out_len = LHKT_LORAHAM_HDR_LEN + len;

    return LHKT_OK;
}


static uint8_t loraham_hdr_byte(size_t pos)
{
    static const uint8_t hdr[LHKT_LORAHAM_HDR_LEN] = {
        LORAHAM_APRS_HDR0,
        LORAHAM_APRS_HDR1,
        LORAHAM_APRS_HDR2
    };

    return hdr[pos];
}

static void loraham_rx_packet_clear(loraham_rx_state_t *state)
{
    if (!state) {
        return;
    }

    state->len = 0;
    state->seen_header = 0;
    state->hdr_len = 0;
}

static void loraham_pending_consume(loraham_rx_state_t *state, size_t len)
{
    if (!state || len == 0) {
        return;
    }

    if (len >= state->pending_len) {
        state->pending_len = 0;
        return;
    }

    memmove(state->pending,
            state->pending + len,
            state->pending_len - len);
    state->pending_len -= len;
}

static int loraham_append_payload(loraham_rx_state_t *state, uint8_t b)
{
    if (!state) {
        return LHKT_ERR;
    }

    if (state->len >= sizeof(state->buf) - 1) {
        return LHKT_ERR_LONG;
    }

    state->buf[state->len++] = b;
    return LHKT_OK;
}

static int loraham_append_hdr_prefix(loraham_rx_state_t *state)
{
    size_t i;
    int ret;

    for (i = 0; i < state->hdr_len; i++) {
        ret = loraham_append_payload(state, loraham_hdr_byte(i));
        if (ret != LHKT_OK) {
            return ret;
        }
    }

    state->hdr_len = 0;
    return LHKT_OK;
}


static int loraham_emit_tnc2(loraham_rx_state_t *state,
                             char *tnc2_out,
                             size_t tnc2_size,
                             size_t *tnc2_len,
                             int flush_partial_header,
                             int keep_header_seen)
{
    int ret;

    if (!state || !tnc2_out || !tnc2_len || tnc2_size == 0) {
        return LHKT_ERR;
    }

    if (flush_partial_header && state->seen_header && state->hdr_len > 0) {
        ret = loraham_append_hdr_prefix(state);
        if (ret != LHKT_OK) {
            loraham_rx_state_init(state);
            *tnc2_len = 0;
            return ret;
        }
    }

    if (!state->seen_header || state->len == 0) {
        state->hdr_len = 0;
        if (!keep_header_seen && state->len == 0) {
            state->seen_header = 0;
        }
        return 0;
    }

    if (state->len >= tnc2_size) {
        loraham_rx_packet_clear(state);
        *tnc2_len = 0;
        return LHKT_ERR_NOSPACE;
    }

    memcpy(tnc2_out, state->buf, state->len);
    tnc2_out[state->len] = '\0';
    *tnc2_len = state->len;

    state->len = 0;
    state->hdr_len = 0;
    state->seen_header = keep_header_seen ? 1 : 0;

    return 1;
}

/*
 * Search for 0x3c 0xff 0x01 before accepting text.
 * data_len == 0 drains queued data or flushes an idle packet.
 */

int loraham_extract_tnc2(loraham_rx_state_t *state,
                         const uint8_t *data,
                         size_t data_len,
                         char *tnc2_out,
                         size_t tnc2_size,
                         size_t *tnc2_len)
{
    uint8_t b;
    int ret;
    int flush_on_empty;

    if (!state || !tnc2_out || !tnc2_len) {
        return LHKT_ERR;
    }

    *tnc2_len = 0;
    flush_on_empty = (data_len == 0 && state->pending_len == 0);

    if (data_len > 0 && !data) {
        return LHKT_ERR;
    }

    if (data_len > 0) {
        if (data_len > sizeof(state->pending) - state->pending_len) {
            loraham_rx_state_init(state);
            return LHKT_ERR_LONG;
        }

        memcpy(state->pending + state->pending_len, data, data_len);
        state->pending_len += data_len;
    }

    while (state->pending_len > 0) {
        b = state->pending[0];
        loraham_pending_consume(state, 1);

        if (!state->seen_header) {
            if (b == loraham_hdr_byte(state->hdr_len)) {
                state->hdr_len++;
                if (state->hdr_len == LHKT_LORAHAM_HDR_LEN) {
                    state->seen_header = 1;
                    state->len = 0;
                    state->hdr_len = 0;
                }
            } else if (b == LORAHAM_APRS_HDR0) {
                state->hdr_len = 1;
            } else {
                state->hdr_len = 0;
            }

            continue;
        }

        if (state->hdr_len > 0) {
            if (b == loraham_hdr_byte(state->hdr_len)) {
                state->hdr_len++;
                if (state->hdr_len == LHKT_LORAHAM_HDR_LEN) {
                    ret = loraham_emit_tnc2(state,
                                            tnc2_out,
                                            tnc2_size,
                                            tnc2_len,
                                            0,
                                            1);
                    if (ret != 0) {
                        return ret;
                    }
                }

                continue;
            }

            ret = loraham_append_hdr_prefix(state);
            if (ret != LHKT_OK) {
                loraham_rx_state_init(state);
                return ret;
            }
        }

        if (b == '\r' || b == '\n') {
            ret = loraham_emit_tnc2(state,
                                    tnc2_out,
                                    tnc2_size,
                                    tnc2_len,
                                    1,
                                    0);
            if (ret != 0) {
                return ret;
            }

            continue;
        }

        if (b == LORAHAM_APRS_HDR0) {
            state->hdr_len = 1;
            continue;
        }

        ret = loraham_append_payload(state, b);
        if (ret != LHKT_OK) {
            loraham_rx_state_init(state);
            return ret;
        }
    }

    if (flush_on_empty) {
        return loraham_emit_tnc2(state,
                                 tnc2_out,
                                 tnc2_size,
                                 tnc2_len,
                                 1,
                                 0);
    }

    return 0;
}

static int loraham_send_config_line(int conf_fd,
                                    const lhkt_config_t *cfg,
                                    double freq,
                                    int have_freq)
{
    char line[256];
    int ret;

    if (conf_fd < 0 || !cfg) {
        return LHKT_ERR;
    }

    if (have_freq) {
        ret = snprintf(line,
                       sizeof(line),
                       "SET MODE=%s FREQ=%.6f SF=%d BW=%.3f CR=%d CRC=%d PREAMBLE=%d SYNC=0x%02X LDRO=%d POWER=%d\n",
                       cfg->mode,
                       freq,
                       cfg->sf,
                       cfg->bw,
                       cfg->cr,
                       cfg->crc,
                       cfg->preamble,
                       cfg->syncword,
                       cfg->ldro,
                       cfg->power);
    } else {
        ret = snprintf(line,
                       sizeof(line),
                       "SET MODE=%s SF=%d BW=%.3f CR=%d CRC=%d PREAMBLE=%d SYNC=0x%02X LDRO=%d POWER=%d\n",
                       cfg->mode,
                       cfg->sf,
                       cfg->bw,
                       cfg->cr,
                       cfg->crc,
                       cfg->preamble,
                       cfg->syncword,
                       cfg->ldro,
                       cfg->power);
    }

    if (ret < 0 || (size_t)ret >= sizeof(line)) {
        return LHKT_ERR_NOSPACE;
    }

    if (loraham_sock_write(conf_fd, (const uint8_t *)line, strlen(line)) < 0) {
        return LHKT_ERR;
    }

    return LHKT_OK;
}

int loraham_send_config_freq(int conf_fd,
                             const lhkt_config_t *cfg,
                             double freq)
{
    if (freq <= 0.0) {
        return LHKT_ERR_FORMAT;
    }

    return loraham_send_config_line(conf_fd, cfg, freq, 1);
}

/*
 * Send initial LoRa config command.
 * RX frequency is the normal receive frequency.
 */
int loraham_send_config(int conf_fd,
                        const lhkt_config_t *cfg)
{
    if (!cfg) {
        return LHKT_ERR;
    }

    if (cfg->have_rx_freq) {
        return loraham_send_config_freq(conf_fd, cfg, cfg->rx_freq);
    }

    return loraham_send_config_line(conf_fd, cfg, 0.0, 0);
}
