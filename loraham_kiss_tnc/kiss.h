#ifndef KISS_H
#define KISS_H

#include <stddef.h>
#include <stdint.h>
#include "loraham_kiss_tnc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KISS_FEND   0xC0
#define KISS_FESC   0xDB
#define KISS_TFEND  0xDC
#define KISS_TFESC  0xDD

#define KISS_CMD_DATA        0
#define KISS_CMD_TXDELAY     1
#define KISS_CMD_PERSIST     2
#define KISS_CMD_SLOTTIME    3
#define KISS_CMD_TXTAIL      4
#define KISS_CMD_FULLDUPLEX  5
#define KISS_CMD_SETHARDWARE 6
#define KISS_CMD_EXIT        15

typedef struct {
    uint8_t port;
    uint8_t command;
    uint8_t data[LHKT_KISS_MAX_FRAME];
    size_t data_len;
} kiss_frame_t;

typedef struct {
    int in_frame;
    int escaped;
    uint8_t buf[LHKT_KISS_MAX_FRAME];
    size_t len;
} kiss_decoder_t;

typedef struct {
    uint8_t txdelay;
    uint8_t persistence;
    uint8_t slottime;
    uint8_t txtail;
    uint8_t fullduplex;
} kiss_params_t;

void kiss_decoder_init(kiss_decoder_t *dec);
void kiss_params_init(kiss_params_t *params);

int kiss_decode_byte(kiss_decoder_t *dec,
                     uint8_t byte,
                     kiss_frame_t *out);

int kiss_handle_command(kiss_params_t *params,
                        const kiss_frame_t *frame);

int kiss_encode_data_frame(const uint8_t *data,
                           size_t data_len,
                           uint8_t *out,
                           size_t out_size,
                           size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif