#include "ax25.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_addr_parse_format(void)
{
    ax25_addr_t addr;
    char out[32];

    assert(ax25_addr_parse("dj0che-10", &addr) == LHKT_OK);
    assert(strcmp(addr.call, "DJ0CHE") == 0);
    assert(addr.ssid == 10);
    assert(addr.repeated == 0);

    assert(ax25_addr_format(&addr, out, sizeof(out)) == LHKT_OK);
    assert(strcmp(out, "DJ0CHE-10") == 0);

    assert(ax25_addr_parse("WIDE1-1*", &addr) == LHKT_OK);
    assert(strcmp(addr.call, "WIDE1") == 0);
    assert(addr.ssid == 1);
    assert(addr.repeated == 1);

    assert(ax25_addr_format(&addr, out, sizeof(out)) == LHKT_OK);
    assert(strcmp(out, "WIDE1-1*") == 0);
}

static void test_addr_accept_boundaries(void)
{
    ax25_addr_t addr;
    char out[32];

    assert(ax25_addr_parse("ABC123", &addr) == LHKT_OK);
    assert(strcmp(addr.call, "ABC123") == 0);
    assert(addr.ssid == 0);
    assert(addr.repeated == 0);

    assert(ax25_addr_format(&addr, out, sizeof(out)) == LHKT_OK);
    assert(strcmp(out, "ABC123") == 0);

    assert(ax25_addr_parse("N0CALL-15", &addr) == LHKT_OK);
    assert(strcmp(addr.call, "N0CALL") == 0);
    assert(addr.ssid == 15);
    assert(addr.repeated == 0);

    assert(ax25_addr_format(&addr, out, sizeof(out)) == LHKT_OK);
    assert(strcmp(out, "N0CALL-15") == 0);
}

static void test_addr_reject_invalid(void)
{
    ax25_addr_t addr;

    assert(ax25_addr_parse("", &addr) == LHKT_ERR_FORMAT);
    assert(ax25_addr_parse("TOOLONG-1", &addr) == LHKT_ERR_FORMAT);
    assert(ax25_addr_parse("DJ0CHE-16", &addr) == LHKT_ERR_FORMAT);
    assert(ax25_addr_parse("DJ0CHE-X", &addr) == LHKT_ERR_FORMAT);
    assert(ax25_addr_parse("DJ0CHE-", &addr) == LHKT_ERR_FORMAT);
    assert(ax25_addr_parse("DJ0CHE-*", &addr) == LHKT_ERR_FORMAT);
    assert(ax25_addr_parse("DJ0C/E", &addr) == LHKT_ERR_FORMAT);
}

static void test_encode_decode_roundtrip(void)
{
    ax25_frame_t in;
    ax25_frame_t out;
    uint8_t raw[LHKT_AX25_MAX_FRAME];
    size_t raw_len = 0;
    const char *payload = ":DC2EH-11:Hello test{01";

    ax25_frame_init(&in);

    assert(ax25_addr_parse("APLG01", &in.dst) == LHKT_OK);
    assert(ax25_addr_parse("DJ0CHE-10", &in.src) == LHKT_OK);
    assert(ax25_addr_parse("WIDE1-1", &in.path[0]) == LHKT_OK);
    assert(ax25_addr_parse("WIDE2-1", &in.path[1]) == LHKT_OK);
    in.path_len = 2;

    memcpy(in.payload, payload, strlen(payload));
    in.payload_len = strlen(payload);

    assert(ax25_encode_ui(&in, raw, sizeof(raw), &raw_len) == LHKT_OK);
    assert(raw_len > 0);

    assert(ax25_decode_ui(raw, raw_len, &out) == LHKT_OK);

    assert(strcmp(out.dst.call, "APLG01") == 0);
    assert(out.dst.ssid == 0);

    assert(strcmp(out.src.call, "DJ0CHE") == 0);
    assert(out.src.ssid == 10);

    assert(out.path_len == 2);
    assert(strcmp(out.path[0].call, "WIDE1") == 0);
    assert(out.path[0].ssid == 1);
    assert(strcmp(out.path[1].call, "WIDE2") == 0);
    assert(out.path[1].ssid == 1);

    assert(out.payload_len == strlen(payload));
    assert(memcmp(out.payload, payload, strlen(payload)) == 0);
}

static void test_decode_reject_short_frame(void)
{
    ax25_frame_t in;
    ax25_frame_t out;
    uint8_t raw[LHKT_AX25_MAX_FRAME];
    size_t raw_len = 0;

    ax25_frame_init(&in);

    assert(ax25_addr_parse("APRS", &in.dst) == LHKT_OK);
    assert(ax25_addr_parse("DJ0CHE-10", &in.src) == LHKT_OK);

    in.payload[0] = 'x';
    in.payload_len = 1;

    assert(ax25_encode_ui(&in, raw, sizeof(raw), &raw_len) == LHKT_OK);
    assert(raw_len > 2);

    assert(ax25_decode_ui(raw, raw_len - 2, &out) == LHKT_ERR_SHORT);
}

static void test_decode_reject_missing_final_address_bit(void)
{
    ax25_frame_t in;
    ax25_frame_t out;
    uint8_t raw[LHKT_AX25_MAX_FRAME];
    size_t raw_len = 0;

    ax25_frame_init(&in);

    assert(ax25_addr_parse("APRS", &in.dst) == LHKT_OK);
    assert(ax25_addr_parse("DJ0CHE-10", &in.src) == LHKT_OK);

    in.payload[0] = 'x';
    in.payload_len = 1;

    assert(ax25_encode_ui(&in, raw, sizeof(raw), &raw_len) == LHKT_OK);

    raw[13] &= (uint8_t)~0x01;

    assert(ax25_decode_ui(raw, raw_len, &out) == LHKT_ERR_FORMAT);
}

static void test_decode_reject_wrong_pid(void)
{
    ax25_frame_t in;
    ax25_frame_t out;
    uint8_t raw[LHKT_AX25_MAX_FRAME];
    size_t raw_len = 0;

    ax25_frame_init(&in);

    assert(ax25_addr_parse("APRS", &in.dst) == LHKT_OK);
    assert(ax25_addr_parse("DJ0CHE-10", &in.src) == LHKT_OK);

    in.payload[0] = 'x';
    in.payload_len = 1;

    assert(ax25_encode_ui(&in, raw, sizeof(raw), &raw_len) == LHKT_OK);

    raw[15] = 0x00; /* Wrong PID */

    assert(ax25_decode_ui(raw, raw_len, &out) == LHKT_ERR_UNSUPPORTED);
}

static void test_reject_non_ui(void)
{
    ax25_frame_t in;
    ax25_frame_t out;
    uint8_t raw[LHKT_AX25_MAX_FRAME];
    size_t raw_len = 0;

    ax25_frame_init(&in);

    assert(ax25_addr_parse("APRS", &in.dst) == LHKT_OK);
    assert(ax25_addr_parse("DJ0CHE-10", &in.src) == LHKT_OK);

    in.payload[0] = 'x';
    in.payload_len = 1;

    assert(ax25_encode_ui(&in, raw, sizeof(raw), &raw_len) == LHKT_OK);

    raw[14] = 0x13; /* Not UI */

    assert(ax25_decode_ui(raw, raw_len, &out) == LHKT_ERR_UNSUPPORTED);
}

int main(void)
{
    test_addr_parse_format();
    test_addr_accept_boundaries();
    test_addr_reject_invalid();
    test_encode_decode_roundtrip();
    test_decode_reject_short_frame();
    test_decode_reject_missing_final_address_bit();
    test_decode_reject_wrong_pid();
    test_reject_non_ui();

    puts("test_ax25: OK");
    return 0;
}
