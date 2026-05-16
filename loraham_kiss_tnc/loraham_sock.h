#ifndef LORAHAM_SOCK_H
#define LORAHAM_SOCK_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include "loraham_kiss_tnc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LORAHAM_APRS_HDR0 0x3c
#define LORAHAM_APRS_HDR1 0xff
#define LORAHAM_APRS_HDR2 0x01
#define LHKT_LORAHAM_RX_PENDING 1024

typedef struct {
    uint8_t buf[LHKT_TNC2_MAX_LINE];
    size_t len;
    int seen_header;
    size_t hdr_len;
    uint8_t pending[LHKT_LORAHAM_RX_PENDING];
    size_t pending_len;
} loraham_rx_state_t;

void loraham_rx_state_init(loraham_rx_state_t *state);

int loraham_sock_connect(const char *path);

ssize_t loraham_sock_read(int fd,
                          uint8_t *buf,
                          size_t buf_size);

ssize_t loraham_sock_write(int fd,
                           const uint8_t *buf,
                           size_t len);

int loraham_build_aprs_packet(const char *tnc2,
                              uint8_t *out,
                              size_t out_size,
                              size_t *out_len);

int loraham_extract_tnc2(loraham_rx_state_t *state,
                         const uint8_t *data,
                         size_t data_len,
                         char *tnc2_out,
                         size_t tnc2_size,
                         size_t *tnc2_len);

int loraham_send_config(int conf_fd,
                        const lhkt_config_t *cfg);

int loraham_send_config_freq(int conf_fd,
                             const lhkt_config_t *cfg,
                             double freq);

#ifdef __cplusplus
}
#endif

#endif
