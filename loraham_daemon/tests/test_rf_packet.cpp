#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "rf_packet.h"

static int g_ok = 0;
static int g_fail = 0;

static void expect_int(const char *name, int actual, int expected)
{
    if (actual == expected) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: expected %d, got %d\n",
               name, expected, actual);
    }
}

static void expect_size(const char *name, size_t actual, size_t expected)
{
    if (actual == expected) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: expected %zu, got %zu\n",
               name, expected, actual);
    }
}

static void expect_str(const char *name, const char *actual, const char *expected)
{
    if (strcmp(actual, expected) == 0) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: expected %s, got %s\n",
               name, expected, actual);
    }
}

static void test_validate_accepts_valid_payload_lengths(void)
{
    uint8_t buf[RF_PACKET_MAX_PAYLOAD_LEN + 1] = {0};

    expect_int("validate one byte",
               rf_packet_validate(buf, 1), RF_PACKET_VALID);
    expect_int("validate max payload",
               rf_packet_validate(buf, RF_PACKET_MAX_PAYLOAD_LEN),
               RF_PACKET_VALID);
}

static void test_validate_rejects_invalid_payloads(void)
{
    uint8_t buf[RF_PACKET_MAX_PAYLOAD_LEN + 1] = {0};

    expect_int("reject empty payload",
               rf_packet_validate(buf, 0), RF_PACKET_ERR_EMPTY);
    expect_int("reject null buffer",
               rf_packet_validate(0, 1), RF_PACKET_ERR_NULL);
    expect_int("reject too long payload",
               rf_packet_validate(buf, RF_PACKET_MAX_PAYLOAD_LEN + 1),
               RF_PACKET_ERR_TOO_LONG);
}

static void test_preview_len_is_bounded(void)
{
    expect_size("preview zero", rf_packet_preview_len(0), 0);
    expect_size("preview one", rf_packet_preview_len(1), 1);
    expect_size("preview exact",
                rf_packet_preview_len(RF_PACKET_PREVIEW_LEN),
                RF_PACKET_PREVIEW_LEN);
    expect_size("preview bounded",
                rf_packet_preview_len(RF_PACKET_PREVIEW_LEN + 1),
                RF_PACKET_PREVIEW_LEN);
    expect_size("preview max payload",
                rf_packet_preview_len(RF_PACKET_MAX_PAYLOAD_LEN),
                RF_PACKET_PREVIEW_LEN);
}

static void test_validation_messages_are_stable(void)
{
    expect_str("valid message",
               rf_packet_validation_message(RF_PACKET_VALID), "ok");
    expect_str("null message",
               rf_packet_validation_message(RF_PACKET_ERR_NULL),
               "null buffer");
    expect_str("empty message",
               rf_packet_validation_message(RF_PACKET_ERR_EMPTY),
               "empty packet");
    expect_str("too long message",
               rf_packet_validation_message(RF_PACKET_ERR_TOO_LONG),
               "packet too long");
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--bin") == 0) {
            if (i + 1 >= argc) {
                printf("Usage: %s [--bin ignored]\n", argv[0]);
                return 2;
            }
            i++;
        } else if (strcmp(argv[i], "--help") == 0 ||
                   strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [--bin ignored]\n", argv[0]);
            return 0;
        } else {
            printf("Usage: %s [--bin ignored]\n", argv[0]);
            return 2;
        }
    }

    test_validate_accepts_valid_payload_lengths();
    test_validate_rejects_invalid_payloads();
    test_preview_len_is_bounded();
    test_validation_messages_are_stable();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
