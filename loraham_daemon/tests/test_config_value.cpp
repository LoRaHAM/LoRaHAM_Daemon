#include "../config_value.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Strict CONFIG value parser tests.
 *
 * These helpers intentionally reject partially parsed values such as
 * "433abc" or "1xyz"; CONFIG apply must not silently coerce them.
 */

static int g_ok = 0;
static int g_fail = 0;

static void expect_int(const char *name, int actual, int expected)
{
    if (actual == expected) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: expected %d, got %d\n", name, expected, actual);
    }
}

static void expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual == expected) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: expected %u, got %u\n", name, expected, actual);
    }
}

static void test_int_exact(void)
{
    int out = 0;

    expect_int("int accepts plain", config_value_parse_int_exact("12", &out), 1);
    expect_int("int plain value", out, 12);

    expect_int("int accepts trim", config_value_parse_int_exact("  -5  ", &out), 1);
    expect_int("int trim value", out, -5);

    expect_int("int rejects suffix", config_value_parse_int_exact("12x", &out), 0);
    expect_int("int rejects float", config_value_parse_int_exact("12.0", &out), 0);
    expect_int("int rejects empty", config_value_parse_int_exact("", &out), 0);
}

static void test_float_exact(void)
{
    float out = 0.0f;

    expect_int("float accepts integer", config_value_parse_float_exact("433", &out), 1);
    expect_int("float integer value", config_value_float_equal(out, 433.0f), 1);

    expect_int("float accepts decimal", config_value_parse_float_exact("31.25", &out), 1);
    expect_int("float decimal value", config_value_float_equal(out, 31.25f), 1);

    expect_int("float rejects suffix", config_value_parse_float_exact("433abc", &out), 0);
    expect_int("float rejects nan", config_value_parse_float_exact("nan", &out), 0);
    expect_int("float rejects inf", config_value_parse_float_exact("inf", &out), 0);
}

static void test_bool01_exact(void)
{
    int out = 0;

    expect_int("bool accepts 0", config_value_parse_bool01_exact("0", &out), 1);
    expect_int("bool 0 value", out, 0);

    expect_int("bool accepts 1", config_value_parse_bool01_exact("1", &out), 1);
    expect_int("bool 1 value", out, 1);

    expect_int("bool rejects 2", config_value_parse_bool01_exact("2", &out), 0);
    expect_int("bool rejects suffix", config_value_parse_bool01_exact("1xyz", &out), 0);
    expect_int("bool rejects true", config_value_parse_bool01_exact("true", &out), 0);
}

static void test_hex_or_dec_u32_exact(void)
{
    uint32_t out = 0;

    expect_int("u32 accepts decimal", config_value_parse_hex_or_dec_u32_exact("4660", &out), 1);
    expect_u32("u32 decimal value", out, 4660);

    expect_int("u32 accepts hex", config_value_parse_hex_or_dec_u32_exact("0x1234", &out), 1);
    expect_u32("u32 hex value", out, 0x1234);

    expect_int("u32 accepts uppercase hex", config_value_parse_hex_or_dec_u32_exact("0X2B", &out), 1);
    expect_u32("u32 uppercase hex value", out, 0x2B);

    expect_int("u32 rejects hex suffix", config_value_parse_hex_or_dec_u32_exact("0x12zz", &out), 0);
    expect_int("u32 rejects decimal suffix", config_value_parse_hex_or_dec_u32_exact("18abc", &out), 0);
    expect_int("u32 rejects negative", config_value_parse_hex_or_dec_u32_exact("-1", &out), 0);
}

static void test_string_helpers(void)
{
    expect_int("trim ascii", config_value_trim_ascii("  ABC \t") == "ABC", 1);
    expect_int("lower ascii", config_value_lower_ascii("LoRa") == "lora", 1);
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

    test_int_exact();
    test_float_exact();
    test_bool01_exact();
    test_hex_or_dec_u32_exact();
    test_string_helpers();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
