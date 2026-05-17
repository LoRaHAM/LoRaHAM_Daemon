#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "rf_packet.h"

static void test_validate_accepts_valid_payload_lengths(void)
{
    uint8_t buf[RF_PACKET_MAX_PAYLOAD_LEN + 1] = {0};

    assert(rf_packet_validate(buf, 1) == RF_PACKET_VALID);
    assert(rf_packet_validate(buf, RF_PACKET_MAX_PAYLOAD_LEN) == RF_PACKET_VALID);
}

static void test_validate_rejects_invalid_payloads(void)
{
    uint8_t buf[RF_PACKET_MAX_PAYLOAD_LEN + 1] = {0};

    assert(rf_packet_validate(buf, 0) == RF_PACKET_ERR_EMPTY);
    assert(rf_packet_validate(0, 1) == RF_PACKET_ERR_NULL);
    assert(rf_packet_validate(buf, RF_PACKET_MAX_PAYLOAD_LEN + 1) ==
           RF_PACKET_ERR_TOO_LONG);
}

static void test_preview_len_is_bounded(void)
{
    assert(rf_packet_preview_len(0) == 0);
    assert(rf_packet_preview_len(1) == 1);
    assert(rf_packet_preview_len(RF_PACKET_PREVIEW_LEN) == RF_PACKET_PREVIEW_LEN);
    assert(rf_packet_preview_len(RF_PACKET_PREVIEW_LEN + 1) ==
           RF_PACKET_PREVIEW_LEN);
    assert(rf_packet_preview_len(RF_PACKET_MAX_PAYLOAD_LEN) ==
           RF_PACKET_PREVIEW_LEN);
}

static void test_validation_messages_are_stable(void)
{
    assert(strcmp(rf_packet_validation_message(RF_PACKET_VALID), "ok") == 0);
    assert(strcmp(rf_packet_validation_message(RF_PACKET_ERR_NULL),
                  "null buffer") == 0);
    assert(strcmp(rf_packet_validation_message(RF_PACKET_ERR_EMPTY),
                  "empty packet") == 0);
    assert(strcmp(rf_packet_validation_message(RF_PACKET_ERR_TOO_LONG),
                  "packet too long") == 0);
}

int main(void)
{
    test_validate_accepts_valid_payload_lengths();
    test_validate_rejects_invalid_payloads();
    test_preview_len_is_bounded();
    test_validation_messages_are_stable();

    return 0;
}
