#ifndef LORAHAM_FRAMED_DATA_H
#define LORAHAM_FRAMED_DATA_H

#include <stddef.h>
#include <stdint.h>

/* --- Framed DATA socket protocol ---------------------------------------- */

#define FRAMED_DATA_HEADER_LEN 3
#define FRAMED_DATA_MAX_RF_PAYLOAD 255
#define FRAMED_DATA_RX_META_LEN 4
#define FRAMED_DATA_RX_PAYLOAD_MAX (FRAMED_DATA_RX_META_LEN + FRAMED_DATA_MAX_RF_PAYLOAD)
#define FRAMED_DATA_RX_FRAME_MAX (FRAMED_DATA_HEADER_LEN + FRAMED_DATA_RX_PAYLOAD_MAX)
#define FRAMED_DATA_TX_RESULT_PAYLOAD_LEN 4
#define FRAMED_DATA_SIGNAL_UNAVAILABLE ((int16_t)(-32767 - 1))

#define FRAMED_DATA_TYPE_RX_PACKET 0x01
#define FRAMED_DATA_TYPE_TX_PACKET 0x02
#define FRAMED_DATA_TYPE_ERROR     0x03
#define FRAMED_DATA_TYPE_TX_RESULT 0x04

#define FRAMED_DATA_TX_STATUS_OK              0
#define FRAMED_DATA_TX_STATUS_BUSY            1
#define FRAMED_DATA_TX_STATUS_CHANNEL_BUSY    2
#define FRAMED_DATA_TX_STATUS_RADIO_NOT_READY 3
#define FRAMED_DATA_TX_STATUS_RADIO_ERROR     4
#define FRAMED_DATA_TX_STATUS_INVALID_PACKET  5
#define FRAMED_DATA_TX_STATUS_INVALID_BAND    6

#define FRAMED_DATA_TX_RESULT_FLAG_MANAGED     0x01
#define FRAMED_DATA_TX_RESULT_FLAG_DEFERRED    0x02
#define FRAMED_DATA_TX_RESULT_FLAG_CAD_TIMEOUT 0x04

int framed_data_type_known(uint8_t frame_type);
int framed_data_rf_payload_len_valid(size_t payload_len);
int framed_data_tx_result_status_valid(uint8_t status);
size_t framed_data_frame_size(uint16_t payload_len);

int framed_data_encode_header(uint8_t *header,
                              size_t header_len,
                              uint8_t frame_type,
                              uint16_t payload_len);

int framed_data_decode_header(const uint8_t *header,
                              size_t header_len,
                              uint8_t *frame_type,
                              uint16_t *payload_len);

int framed_data_encode_frame(uint8_t *frame,
                             size_t frame_len,
                             uint8_t frame_type,
                             const uint8_t *payload,
                             uint16_t payload_len);

int framed_data_encode_rx_packet(uint8_t *frame,
                                 size_t frame_len,
                                 int16_t rssi_cdbm,
                                 int16_t snr_cdb,
                                 const uint8_t *rf_payload,
                                 uint16_t rf_len);

int framed_data_encode_tx_result(uint8_t *frame,
                                 size_t frame_len,
                                 uint8_t status,
                                 uint8_t flags,
                                 uint16_t seq);

#endif
