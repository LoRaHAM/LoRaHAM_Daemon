#include "../config_apply.h"

#include <stdio.h>
#include <string.h>

/* --- Transactional CONFIG apply integration tests ------------------------ */

static int g_ok = 0;
static int g_fail = 0;

struct FakeRadio {
    int begin_count;
    int begin_fsk_count;
    int lora_apply_count;
    int fsk_apply_count;
    int begin_result;
    int begin_fsk_result;

    FakeRadio() :
        begin_count(0),
        begin_fsk_count(0),
        lora_apply_count(0),
        fsk_apply_count(0),
        begin_result(RADIOLIB_ERR_NONE),
        begin_fsk_result(RADIOLIB_ERR_NONE)
    {
    }

    int begin()
    {
        begin_count++;
        return begin_result;
    }

    int beginFSK()
    {
        begin_fsk_count++;
        return begin_fsk_result;
    }
};

void apply_lora_param(FakeRadio &radio,
                      const char *tag,
                      const std::string &key,
                      const std::string &val)
{
    (void)tag;
    (void)key;
    (void)val;

    radio.lora_apply_count++;
}

void apply_fsk_param(FakeRadio &radio,
                     const char *tag,
                     const std::string &key,
                     const std::string &val)
{
    (void)tag;
    (void)key;
    (void)val;

    radio.fsk_apply_count++;
}

/* --- Test helpers --- */

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

static void apply_cmd(FakeRadio &radio,
                      const char *cmd,
                      volatile RadioMode_t &mode,
                      volatile bool &getrssi)
{
    parse_and_apply_config_generic<FakeRadio>(radio, "TEST", cmd, mode, getrssi);
}

/* --- Tests --------------------------------------------------------------- */

static void test_invalid_getrssi_has_no_side_effects(void)
{
    FakeRadio radio;
    volatile RadioMode_t mode = RADIO_MODE_LORA;
    volatile bool getrssi = false;

    apply_cmd(radio, "SET GETRSSI=2 MODE=FSK", mode, getrssi);

    expect_int("invalid getrssi mode unchanged", mode, RADIO_MODE_LORA);
    expect_int("invalid getrssi flag unchanged", getrssi, false);
    expect_int("invalid getrssi beginFSK not called", radio.begin_fsk_count, 0);
    expect_int("invalid getrssi lora apply not called", radio.lora_apply_count, 0);
    expect_int("invalid getrssi fsk apply not called", radio.fsk_apply_count, 0);
}

static void test_invalid_fsk_value_has_no_mode_or_getrssi_side_effects(void)
{
    FakeRadio radio;
    volatile RadioMode_t mode = RADIO_MODE_LORA;
    volatile bool getrssi = false;

    apply_cmd(radio, "SET MODE=FSK GETRSSI=1 BR=bad", mode, getrssi);

    expect_int("invalid fsk mode unchanged", mode, RADIO_MODE_LORA);
    expect_int("invalid fsk getrssi unchanged", getrssi, false);
    expect_int("invalid fsk beginFSK not called", radio.begin_fsk_count, 0);
    expect_int("invalid fsk fsk apply not called", radio.fsk_apply_count, 0);
}

static void test_malformed_token_has_no_side_effects(void)
{
    FakeRadio radio;
    volatile RadioMode_t mode = RADIO_MODE_LORA;
    volatile bool getrssi = false;

    apply_cmd(radio, "SET MODE=FSK GETRSSI=1 BROKEN", mode, getrssi);

    expect_int("malformed mode unchanged", mode, RADIO_MODE_LORA);
    expect_int("malformed getrssi unchanged", getrssi, false);
    expect_int("malformed beginFSK not called", radio.begin_fsk_count, 0);
    expect_int("malformed fsk apply not called", radio.fsk_apply_count, 0);
}

static void test_valid_mode_and_getrssi_still_apply(void)
{
    FakeRadio radio;
    volatile RadioMode_t mode = RADIO_MODE_LORA;
    volatile bool getrssi = false;

    apply_cmd(radio, "SET MODE=FSK GETRSSI=1", mode, getrssi);

    expect_int("valid mode switched", mode, RADIO_MODE_FSK);
    expect_int("valid getrssi set", getrssi, true);
    expect_int("valid beginFSK called", radio.begin_fsk_count, 1);
}

static void test_valid_lora_parameter_still_applies(void)
{
    FakeRadio radio;
    volatile RadioMode_t mode = RADIO_MODE_LORA;
    volatile bool getrssi = false;

    apply_cmd(radio, "SET FREQ=433.900", mode, getrssi);

    expect_int("valid lora param apply count", radio.lora_apply_count, 1);
    expect_int("valid lora mode unchanged", mode, RADIO_MODE_LORA);
    expect_int("valid lora getrssi unchanged", getrssi, false);
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

    test_invalid_getrssi_has_no_side_effects();
    test_invalid_fsk_value_has_no_mode_or_getrssi_side_effects();
    test_malformed_token_has_no_side_effects();
    test_valid_mode_and_getrssi_still_apply();
    test_valid_lora_parameter_still_applies();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
