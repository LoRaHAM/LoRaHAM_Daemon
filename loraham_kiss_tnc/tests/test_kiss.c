#include "kiss.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_encode_escape(void)
{
    const uint8_t in[] = { 0x01, KISS_FEND, KISS_FESC, 0x02 };
    uint8_t out[64];
    size_t out_len = 0;

    const uint8_t expected[] = {
        KISS_FEND,
        0x00,
        0x01,
        KISS_FESC, KISS_TFEND,
        KISS_FESC, KISS_TFESC,
        0x02,
        KISS_FEND
    };

    assert(kiss_encode_data_frame(in, sizeof(in), out, sizeof(out), &out_len) == LHKT_OK);
    assert(out_len == sizeof(expected));
    assert(memcmp(out, expected, sizeof(expected)) == 0);
}

static void test_decode_escape(void)
{
    const uint8_t encoded[] = {
        KISS_FEND,
        0x00,
        0x01,
        KISS_FESC, KISS_TFEND,
        KISS_FESC, KISS_TFESC,
        0x02,
        KISS_FEND
    };

    kiss_decoder_t dec;
    kiss_frame_t frame;
    int ret = 0;
    size_t i;

    const uint8_t expected_data[] = {
        0x01, KISS_FEND, KISS_FESC, 0x02
    };

    kiss_decoder_init(&dec);
    memset(&frame, 0, sizeof(frame));

    for (i = 0; i < sizeof(encoded); i++) {
        ret = kiss_decode_byte(&dec, encoded[i], &frame);
    }

    assert(ret == 1);
    assert(frame.port == 0);
    assert(frame.command == KISS_CMD_DATA);
    assert(frame.data_len == sizeof(expected_data));
    assert(memcmp(frame.data, expected_data, sizeof(expected_data)) == 0);
}

static void test_ignore_empty_frames(void)
{
    const uint8_t encoded[] = {
        KISS_FEND,
        KISS_FEND,
        KISS_FEND
    };

    kiss_decoder_t dec;
    kiss_frame_t frame;
    int ret = 0;
    size_t i;

    kiss_decoder_init(&dec);
    memset(&frame, 0, sizeof(frame));

    for (i = 0; i < sizeof(encoded); i++) {
        ret = kiss_decode_byte(&dec, encoded[i], &frame);
        assert(ret == 0);
    }
}

static void test_command_params(void)
{
    kiss_params_t params;
    kiss_frame_t frame;

    kiss_params_init(&params);
    memset(&frame, 0, sizeof(frame));

    frame.command = KISS_CMD_TXDELAY;
    frame.data[0] = 42;
    frame.data_len = 1;

    assert(kiss_handle_command(&params, &frame) == LHKT_OK);
    assert(params.txdelay == 42);

    frame.command = KISS_CMD_PERSIST;
    frame.data[0] = 63;
    frame.data_len = 1;

    assert(kiss_handle_command(&params, &frame) == LHKT_OK);
    assert(params.persistence == 63);

    frame.command = KISS_CMD_SETHARDWARE;
    frame.data_len = 0;

    assert(kiss_handle_command(&params, &frame) == LHKT_OK);
}

static void test_bad_escape_drops_frame(void)
{
    kiss_decoder_t dec;
    kiss_frame_t frame;

    kiss_decoder_init(&dec);
    memset(&frame, 0, sizeof(frame));

    assert(kiss_decode_byte(&dec, KISS_FEND, &frame) == 0);
    assert(kiss_decode_byte(&dec, 0x00, &frame) == 0);
    assert(kiss_decode_byte(&dec, KISS_FESC, &frame) == 0);
    assert(kiss_decode_byte(&dec, 0x11, &frame) == LHKT_ERR_FORMAT);
}


static void test_nonzero_port_decode(void)
{
    const uint8_t encoded[] = {
        KISS_FEND,
        0x10,
        'X',
        KISS_FEND
    };

    kiss_decoder_t dec;
    kiss_frame_t frame;
    int ret = 0;
    size_t i;

    kiss_decoder_init(&dec);
    memset(&frame, 0, sizeof(frame));

    for (i = 0; i < sizeof(encoded); i++) {
        ret = kiss_decode_byte(&dec, encoded[i], &frame);
    }

    assert(ret == 1);
    assert(frame.port == 1);
    assert(frame.command == KISS_CMD_DATA);
    assert(frame.data_len == 1);
    assert(frame.data[0] == 'X');
}

static void test_bad_escape_recovers_next_frame(void)
{
    const uint8_t valid[] = {
        KISS_FEND,
        0x00,
        'O',
        'K',
        KISS_FEND
    };

    kiss_decoder_t dec;
    kiss_frame_t frame;
    int ret = 0;
    size_t i;

    kiss_decoder_init(&dec);
    memset(&frame, 0, sizeof(frame));

    assert(kiss_decode_byte(&dec, KISS_FEND, &frame) == 0);
    assert(kiss_decode_byte(&dec, 0x00, &frame) == 0);
    assert(kiss_decode_byte(&dec, KISS_FESC, &frame) == 0);
    assert(kiss_decode_byte(&dec, 0x11, &frame) == LHKT_ERR_FORMAT);

    for (i = 0; i < sizeof(valid); i++) {
        ret = kiss_decode_byte(&dec, valid[i], &frame);
    }

    assert(ret == 1);
    assert(frame.port == 0);
    assert(frame.command == KISS_CMD_DATA);
    assert(frame.data_len == 2);
    assert(frame.data[0] == 'O');
    assert(frame.data[1] == 'K');
}

static void test_oversized_frame_drops_and_recovers(void)
{
    const uint8_t valid[] = {
        KISS_FEND,
        0x00,
        'R',
        KISS_FEND
    };

    kiss_decoder_t dec;
    kiss_frame_t frame;
    int ret = 0;
    size_t i;

    kiss_decoder_init(&dec);
    memset(&frame, 0, sizeof(frame));

    assert(kiss_decode_byte(&dec, KISS_FEND, &frame) == 0);

    for (i = 0; i < LHKT_KISS_MAX_FRAME; i++) {
        uint8_t b = (i == 0) ? 0x00 : 'A';
        assert(kiss_decode_byte(&dec, b, &frame) == 0);
    }

    assert(kiss_decode_byte(&dec, 'B', &frame) == LHKT_ERR_LONG);

    for (i = 0; i < sizeof(valid); i++) {
        ret = kiss_decode_byte(&dec, valid[i], &frame);
    }

    assert(ret == 1);
    assert(frame.port == 0);
    assert(frame.command == KISS_CMD_DATA);
    assert(frame.data_len == 1);
    assert(frame.data[0] == 'R');
}

static void test_encode_rejects_small_output_for_escaped_payload(void)
{
    const uint8_t in[] = { KISS_FEND };
    uint8_t out[4];
    size_t out_len = 123;

    assert(kiss_encode_data_frame(in, sizeof(in), out, sizeof(out), &out_len) == LHKT_ERR_NOSPACE);
    assert(out_len == 0);
}

static void test_encode_rejects_invalid_args(void)
{
    const uint8_t in[] = { 'A' };
    uint8_t out[8];
    size_t out_len = 0;

    assert(kiss_encode_data_frame(NULL, sizeof(in), out, sizeof(out), &out_len) == LHKT_ERR);
    assert(kiss_encode_data_frame(in, sizeof(in), NULL, sizeof(out), &out_len) == LHKT_ERR);
    assert(kiss_encode_data_frame(in, sizeof(in), out, sizeof(out), NULL) == LHKT_ERR);
}

int main(void)
{
    test_encode_escape();
    test_decode_escape();
    test_ignore_empty_frames();
    test_command_params();
    test_bad_escape_drops_frame();
    test_nonzero_port_decode();
    test_bad_escape_recovers_next_frame();
    test_oversized_frame_drops_and_recovers();
    test_encode_rejects_small_output_for_escaped_payload();
    test_encode_rejects_invalid_args();

    puts("test_kiss: OK");
    return 0;
}
