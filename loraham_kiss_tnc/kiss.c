#include "kiss.h"

#include <string.h>

/*
 * KISS is a byte-stream protocol.
 * Frames are delimited by FEND and special bytes are escaped.
 */

void kiss_decoder_init(kiss_decoder_t *dec)
{
    if (!dec) {
        return;
    }

    memset(dec, 0, sizeof(*dec));
}

void kiss_params_init(kiss_params_t *params)
{
    if (!params) {
        return;
    }

    memset(params, 0, sizeof(*params));
}

/*
 * Reset only the current frame state.
 * The decoder stays ready for the next FEND.
 */
static void kiss_decoder_reset_frame(kiss_decoder_t *dec)
{
    dec->escaped = 0;
    dec->len = 0;
}

/*
 * Convert one completed raw KISS frame into kiss_frame_t.
 * dec->buf[0] is the KISS command byte.
 */
static int kiss_make_frame(const kiss_decoder_t *dec, kiss_frame_t *out)
{
    uint8_t cmd;

    if (!dec || !out) {
        return LHKT_ERR;
    }

    if (dec->len < 1) {
        return 0;
    }

    cmd = dec->buf[0];

    memset(out, 0, sizeof(*out));
    out->port = (cmd >> 4) & 0x0f;
    out->command = cmd & 0x0f;
    out->data_len = dec->len - 1;

    if (out->data_len > 0) {
        memcpy(out->data, dec->buf + 1, out->data_len);
    }

    return 1;
}

/*
 * Feed one byte into the streaming decoder.
 *
 * Return values:
 *   1  complete frame in out
 *   0  no complete frame yet
 *  <0  malformed or too large frame was dropped
 */
int kiss_decode_byte(kiss_decoder_t *dec,
                     uint8_t byte,
                     kiss_frame_t *out)
{
    int ret;

    if (!dec || !out) {
        return LHKT_ERR;
    }

    if (byte == KISS_FEND) {
        if (!dec->in_frame) {
            dec->in_frame = 1;
            kiss_decoder_reset_frame(dec);
            return 0;
        }

        ret = kiss_make_frame(dec, out);
        kiss_decoder_reset_frame(dec);
        return ret;
    }

    if (!dec->in_frame) {
        return 0;
    }

    if (dec->escaped) {
        if (byte == KISS_TFEND) {
            byte = KISS_FEND;
        } else if (byte == KISS_TFESC) {
            byte = KISS_FESC;
        } else {
            kiss_decoder_reset_frame(dec);
            return LHKT_ERR_FORMAT;
        }

        dec->escaped = 0;
    } else if (byte == KISS_FESC) {
        dec->escaped = 1;
        return 0;
    }

    if (dec->len >= sizeof(dec->buf)) {
        kiss_decoder_reset_frame(dec);
        return LHKT_ERR_LONG;
    }

    dec->buf[dec->len++] = byte;
    return 0;
}

/*
 * Store known KISS parameters so clients can send them harmlessly.
 * LoRaHAM does not use these classic modem/PTT timing values.
 */
int kiss_handle_command(kiss_params_t *params,
                        const kiss_frame_t *frame)
{
    if (!frame) {
        return LHKT_ERR;
    }

    switch (frame->command) {
    case KISS_CMD_DATA:
        return LHKT_OK;

    case KISS_CMD_TXDELAY:
        if (params && frame->data_len >= 1) {
            params->txdelay = frame->data[0];
        }
        return LHKT_OK;

    case KISS_CMD_PERSIST:
        if (params && frame->data_len >= 1) {
            params->persistence = frame->data[0];
        }
        return LHKT_OK;

    case KISS_CMD_SLOTTIME:
        if (params && frame->data_len >= 1) {
            params->slottime = frame->data[0];
        }
        return LHKT_OK;

    case KISS_CMD_TXTAIL:
        if (params && frame->data_len >= 1) {
            params->txtail = frame->data[0];
        }
        return LHKT_OK;

    case KISS_CMD_FULLDUPLEX:
        if (params && frame->data_len >= 1) {
            params->fullduplex = frame->data[0];
        }
        return LHKT_OK;

    case KISS_CMD_SETHARDWARE:
    case KISS_CMD_EXIT:
    default:
        return LHKT_OK;
    }
}

/*
 * Append one byte to an encoded KISS frame.
 * Escape FEND and FESC as required by KISS.
 */
static int kiss_put_escaped(uint8_t byte,
                            uint8_t *out,
                            size_t out_size,
                            size_t *pos)
{
    if (!out || !pos) {
        return LHKT_ERR;
    }

    if (byte == KISS_FEND) {
        if (*pos + 2 > out_size) {
            return LHKT_ERR_NOSPACE;
        }
        out[(*pos)++] = KISS_FESC;
        out[(*pos)++] = KISS_TFEND;
        return LHKT_OK;
    }

    if (byte == KISS_FESC) {
        if (*pos + 2 > out_size) {
            return LHKT_ERR_NOSPACE;
        }
        out[(*pos)++] = KISS_FESC;
        out[(*pos)++] = KISS_TFESC;
        return LHKT_OK;
    }

    if (*pos + 1 > out_size) {
        return LHKT_ERR_NOSPACE;
    }

    out[(*pos)++] = byte;
    return LHKT_OK;
}

/*
 * Encode an AX.25 frame as KISS data frame on port 0.
 */
int kiss_encode_data_frame(const uint8_t *data,
                           size_t data_len,
                           uint8_t *out,
                           size_t out_size,
                           size_t *out_len)
{
    size_t pos;
    size_t i;
    int ret;

    if (!data || !out || !out_len) {
        return LHKT_ERR;
    }

    *out_len = 0;
    pos = 0;

    if (out_size < 3) {
        return LHKT_ERR_NOSPACE;
    }

    out[pos++] = KISS_FEND;

    ret = kiss_put_escaped(KISS_CMD_DATA, out, out_size, &pos);
    if (ret != LHKT_OK) {
        return ret;
    }

    for (i = 0; i < data_len; i++) {
        ret = kiss_put_escaped(data[i], out, out_size, &pos);
        if (ret != LHKT_OK) {
            return ret;
        }
    }

    if (pos + 1 > out_size) {
        return LHKT_ERR_NOSPACE;
    }

    out[pos++] = KISS_FEND;
    *out_len = pos;

    return LHKT_OK;
}