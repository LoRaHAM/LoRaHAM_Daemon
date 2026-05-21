#ifndef AX25_H
#define AX25_H

#include <stddef.h>
#include <stdint.h>
#include "loraham_kiss_tnc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char call[7];
    uint8_t ssid;
    uint8_t repeated;
} ax25_addr_t;

typedef struct {
    ax25_addr_t dst;
    ax25_addr_t src;

    ax25_addr_t path[LHKT_AX25_MAX_REPEATERS];
    size_t path_len;

    uint8_t payload[LHKT_AX25_MAX_PAYLOAD];
    size_t payload_len;
} ax25_frame_t;

void ax25_frame_init(ax25_frame_t *frame);

int ax25_addr_parse(const char *text, ax25_addr_t *addr);
int ax25_addr_format(const ax25_addr_t *addr, char *out, size_t out_size);

int ax25_encode_ui(const ax25_frame_t *frame,
                   uint8_t *out,
                   size_t out_size,
                   size_t *out_len);

int ax25_decode_ui(const uint8_t *data,
                   size_t data_len,
                   ax25_frame_t *frame);

#ifdef __cplusplus
}
#endif

#endif
