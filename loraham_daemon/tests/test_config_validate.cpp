#include "../config_validate.h"

#include "../config_parser.h"
#include "../radio_channel.h"

#include <stdio.h>
#include <string.h>

/* --- Transactional CONFIG validation tests --- */

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

static void expect_str(const char *name,
                       const std::string &actual,
                       const char *expected)
{
    if (actual == expected) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: expected %s, got %s\n",
               name, expected, actual.c_str());
    }
}

static ConfigValidationResult validate(const char *cmd,
                                       RadioMode_t current_mode,
                                       int *ok)
{
    ConfigCommand parsed = config_parse_command(cmd);
    ConfigValidationResult result;

    *ok = config_validate_command(parsed, current_mode, &result);

    return result;
}

static void test_valid_lora_command(void)
{
    int ok = 0;
    ConfigValidationResult result =
        validate("SET GETRSSI=1 SF=12 BW=125 FREQ=433.900",
                 RADIO_MODE_LORA, &ok);

    expect_int("valid lora command", ok, 1);
    expect_int("valid lora result flag", result.valid, 1);
    expect_int("valid lora target", result.target_mode, RADIO_MODE_LORA);
}

static void test_invalid_lora_value_rejects_whole_command(void)
{
    int ok = 0;
    ConfigValidationResult result =
        validate("SET GETRSSI=1 SF=12 FREQ=433abc",
                 RADIO_MODE_LORA, &ok);

    expect_int("invalid lora command rejected", ok, 0);
    expect_int("invalid lora result flag", result.valid, 0);
    expect_str("invalid lora key", result.key, "FREQ");
}

static void test_invalid_getrssi_rejects_whole_command(void)
{
    int ok = 0;
    ConfigValidationResult result =
        validate("SET GETRSSI=2 SF=12",
                 RADIO_MODE_LORA, &ok);

    expect_int("invalid getrssi rejected", ok, 0);
    expect_str("invalid getrssi key", result.key, "GETRSSI");
}

static void test_valid_fsk_target_mode(void)
{
    int ok = 0;
    ConfigValidationResult result =
        validate("SET MODE=FSK BR=1.2 RXBW=12.5 SYNC=0x2DD4",
                 RADIO_MODE_LORA, &ok);

    expect_int("valid fsk command", ok, 1);
    expect_int("valid fsk target", result.target_mode, RADIO_MODE_FSK);
}

static void test_invalid_fsk_value_rejects_before_mode_switch(void)
{
    int ok = 0;
    ConfigValidationResult result =
        validate("SET MODE=FSK BR=bad RXBW=12.5",
                 RADIO_MODE_LORA, &ok);

    expect_int("invalid fsk command rejected", ok, 0);
    expect_int("invalid fsk target still reported", result.target_mode, RADIO_MODE_FSK);
    expect_str("invalid fsk key", result.key, "BR");
}

static void test_invalid_mode_rejects_whole_command(void)
{
    int ok = 0;
    ConfigValidationResult result =
        validate("SET MODE=BAD SF=12",
                 RADIO_MODE_LORA, &ok);

    expect_int("invalid mode rejected", ok, 0);
    expect_str("invalid mode key", result.key, "MODE");
}

static void test_ignored_wrong_mode_keys_preserve_compatibility(void)
{
    int ok = 0;
    ConfigValidationResult result =
        validate("SET BR=bad RXBW=also_bad",
                 RADIO_MODE_LORA, &ok);

    expect_int("fsk keys ignored in lora mode stay accepted", ok, 1);
    expect_int("ignored fsk target", result.target_mode, RADIO_MODE_LORA);

    result = validate("SET SF=bad LDRO=bad",
                      RADIO_MODE_FSK, &ok);

    expect_int("lora keys ignored in fsk mode stay accepted", ok, 1);
    expect_int("ignored lora target", result.target_mode, RADIO_MODE_FSK);
}

static void test_unknown_keys_preserve_compatibility(void)
{
    int ok = 0;
    ConfigValidationResult result =
        validate("SET BAD=xyz SF=12",
                 RADIO_MODE_LORA, &ok);

    expect_int("unknown key ignored", ok, 1);
    expect_int("unknown key result flag", result.valid, 1);
}


static void test_malformed_token_rejects_whole_command(void)
{
    int ok = 0;
    ConfigValidationResult result =
        validate("SET GETRSSI=1 SF=12 BROKEN",
                 RADIO_MODE_LORA, &ok);

    expect_int("malformed token rejected", ok, 0);
    expect_int("malformed result flag", result.valid, 0);
    expect_str("malformed token key", result.key, "BROKEN");
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

    test_valid_lora_command();
    test_invalid_lora_value_rejects_whole_command();
    test_invalid_getrssi_rejects_whole_command();
    test_valid_fsk_target_mode();
    test_invalid_fsk_value_rejects_before_mode_switch();
    test_invalid_mode_rejects_whole_command();
    test_ignored_wrong_mode_keys_preserve_compatibility();
    test_unknown_keys_preserve_compatibility();
    test_malformed_token_rejects_whole_command();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
