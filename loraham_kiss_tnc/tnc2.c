#include "tnc2.h"

#include <stdio.h>
#include <string.h>

/*
 * TNC2 format:
 *   SRC>DST,PATH:PAYLOAD
 *
 * Only the address header is interpreted here.
 * The APRS payload is copied unchanged.
 */

int tnc2_strip_eol(char *line)
{
    size_t len;

    if (!line) {
        return LHKT_ERR;
    }

    len = strlen(line);

    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[len - 1] = '\0';
        len--;
    }

    return LHKT_OK;
}

static int tnc2_parse_path(char *addr_part, ax25_frame_t *frame)
{
    char *p;
    char *comma;
    int ret;

    if (!addr_part || !frame || addr_part[0] == '\0') {
        return LHKT_ERR_FORMAT;
    }

    p = addr_part;
    comma = strchr(p, ',');

    if (comma) {
        *comma = '\0';
    }

    ret = ax25_addr_parse(p, &frame->dst);
    if (ret != LHKT_OK) {
        return ret;
    }

    if (!comma) {
        return LHKT_OK;
    }

    p = comma + 1;
    if (*p == '\0') {
        return LHKT_ERR_FORMAT;
    }

    while (p && *p != '\0') {
        if (frame->path_len >= LHKT_AX25_MAX_REPEATERS) {
            return LHKT_ERR_LONG;
        }

        comma = strchr(p, ',');
        if (comma) {
            *comma = '\0';
        }

        if (*p == '\0') {
            return LHKT_ERR_FORMAT;
        }

        ret = ax25_addr_parse(p, &frame->path[frame->path_len]);
        if (ret != LHKT_OK) {
            return ret;
        }

        frame->path_len++;

        if (!comma) {
            break;
        }

        p = comma + 1;
    }

    return LHKT_OK;
}

int tnc2_parse_line(const char *line, ax25_frame_t *frame)
{
    char buf[LHKT_TNC2_MAX_LINE];
    char *gt;
    char *sep;
    char *payload;
    size_t line_len;
    size_t payload_len;
    int ret;

    if (!line || !frame) {
        return LHKT_ERR;
    }

    line_len = strlen(line);
    if (line_len >= sizeof(buf)) {
        return LHKT_ERR_LONG;
    }

    snprintf(buf, sizeof(buf), "%s", line);
    tnc2_strip_eol(buf);

    ax25_frame_init(frame);

    gt = strchr(buf, '>');
    if (!gt || gt == buf) {
        return LHKT_ERR_FORMAT;
    }

    sep = strchr(gt + 1, ':');
    if (!sep) {
        return LHKT_ERR_FORMAT;
    }

    *gt = '\0';
    *sep = '\0';

    payload = sep + 1;
    payload_len = strlen(payload);

    if (payload_len > LHKT_AX25_MAX_PAYLOAD) {
        return LHKT_ERR_LONG;
    }

    ret = ax25_addr_parse(buf, &frame->src);
    if (ret != LHKT_OK) {
        return ret;
    }

    ret = tnc2_parse_path(gt + 1, frame);
    if (ret != LHKT_OK) {
        return ret;
    }

    if (payload_len > 0) {
        memcpy(frame->payload, payload, payload_len);
    }

    frame->payload_len = payload_len;

    return LHKT_OK;
}

static int tnc2_append_mem(char *out,
                           size_t out_size,
                           size_t *pos,
                           const void *data,
                           size_t len)
{
    if (!out || !pos || !data) {
        return LHKT_ERR;
    }

    if (*pos + len >= out_size) {
        return LHKT_ERR_NOSPACE;
    }

    memcpy(out + *pos, data, len);
    *pos += len;
    out[*pos] = '\0';

    return LHKT_OK;
}

static int tnc2_append_str(char *out,
                           size_t out_size,
                           size_t *pos,
                           const char *text)
{
    if (!text) {
        return LHKT_ERR;
    }

    return tnc2_append_mem(out, out_size, pos, text, strlen(text));
}

int tnc2_format_line(const ax25_frame_t *frame,
                     char *out,
                     size_t out_size,
                     size_t *out_len)
{
    char addr[32];
    size_t pos;
    size_t i;
    int ret;

    if (!frame || !out || !out_len || out_size == 0) {
        return LHKT_ERR;
    }

    *out_len = 0;
    out[0] = '\0';
    pos = 0;

    if (frame->path_len > LHKT_AX25_MAX_REPEATERS) {
        return LHKT_ERR_LONG;
    }

    for (i = 0; i < frame->payload_len; i++) {
        if (frame->payload[i] == 0) {
            return LHKT_ERR_FORMAT;
        }
    }

    ret = ax25_addr_format(&frame->src, addr, sizeof(addr));
    if (ret != LHKT_OK) {
        return ret;
    }

    ret = tnc2_append_str(out, out_size, &pos, addr);
    if (ret != LHKT_OK) {
        return ret;
    }

    ret = tnc2_append_str(out, out_size, &pos, ">");
    if (ret != LHKT_OK) {
        return ret;
    }

    ret = ax25_addr_format(&frame->dst, addr, sizeof(addr));
    if (ret != LHKT_OK) {
        return ret;
    }

    ret = tnc2_append_str(out, out_size, &pos, addr);
    if (ret != LHKT_OK) {
        return ret;
    }

    for (i = 0; i < frame->path_len; i++) {
        ret = tnc2_append_str(out, out_size, &pos, ",");
        if (ret != LHKT_OK) {
            return ret;
        }

        ret = ax25_addr_format(&frame->path[i], addr, sizeof(addr));
        if (ret != LHKT_OK) {
            return ret;
        }

        ret = tnc2_append_str(out, out_size, &pos, addr);
        if (ret != LHKT_OK) {
            return ret;
        }
    }

    ret = tnc2_append_str(out, out_size, &pos, ":");
    if (ret != LHKT_OK) {
        return ret;
    }

    if (frame->payload_len > LHKT_AX25_MAX_PAYLOAD) {
        return LHKT_ERR_LONG;
    }

    if (frame->payload_len > 0) {
        ret = tnc2_append_mem(out,
                              out_size,
                              &pos,
                              frame->payload,
                              frame->payload_len);
        if (ret != LHKT_OK) {
            return ret;
        }
    }

    *out_len = pos;

    return LHKT_OK;
}
