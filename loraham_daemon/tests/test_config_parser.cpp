#include "../config_parser.h"

#include <stdio.h>
#include <string.h>

/*
 * Config parser unit tests.
 *
 * These tests lock the current tokenizer behavior before more config logic
 * is moved out of the daemon.
 */

static int g_ok = 0;
static int g_fail = 0;

/* --- Test helpers --- */

static void expect_true(const char *name, bool value)
{
    if (value) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s\n", name);
    }
}

static void expect_false(const char *name, bool value)
{
    expect_true(name, !value);
}

static void expect_str(const char *name, const std::string &actual, const char *expected)
{
    if (actual == expected) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: expected '%s', got '%s'\n",
               name, expected, actual.c_str());
    }
}

static void expect_size(const char *name, size_t actual, size_t expected)
{
    if (actual == expected) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: expected %zu, got %zu\n", name, expected, actual);
    }
}

static void expect_token(const char *name, const ConfigCommand &cmd,
                         size_t index, const char *key, const char *value)
{
    if (index >= cmd.tokens.size()) {
        g_fail++;
        printf("[FAIL] %s: missing token %zu\n", name, index);
        return;
    }

    if (cmd.tokens[index].first == key && cmd.tokens[index].second == value) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: expected %s=%s, got %s=%s\n",
               name,
               key,
               value,
               cmd.tokens[index].first.c_str(),
               cmd.tokens[index].second.c_str());
    }
}


static void expect_malformed_token(const char *name, const ConfigCommand &cmd,
                                   size_t index, const char *value)
{
    if (index >= cmd.malformed_tokens.size()) {
        g_fail++;
        printf("[FAIL] %s: missing malformed token %zu\n", name, index);
        return;
    }

    if (cmd.malformed_tokens[index] == value) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: expected malformed '%s', got '%s'\n",
               name, value, cmd.malformed_tokens[index].c_str());
    }
}


/* --- Parser behavior tests --- */

static void test_unknown_command(void)
{
    ConfigCommand cmd = config_parse_command("BOGUS\r\n");

    expect_false("unknown command is not SET", cmd.is_set);
    expect_str("unknown command text is trimmed", cmd.text, "BOGUS");
    expect_size("unknown command has no tokens", cmd.tokens.size(), 0);
}

static void test_set_without_params(void)
{
    ConfigCommand cmd = config_parse_command("SET\n");

    expect_true("SET without params is SET", cmd.is_set);
    expect_false("SET without params has no params", cmd.has_params);
    expect_size("SET without params has no tokens", cmd.tokens.size(), 0);
}

static void test_lora_command(void)
{
    ConfigCommand cmd = config_parse_command(
        "SET MODE=LORA FREQ=433.775 SF=12 BW=125 GETRSSI=1\n");

    expect_true("LoRa command is SET", cmd.is_set);
    expect_true("LoRa command has params", cmd.has_params);
    expect_str("MODE value is uppercase", cmd.mode, "LORA");
    expect_size("LoRa command token count", cmd.tokens.size(), 4);

    expect_token("LoRa token FREQ", cmd, 0, "FREQ", "433.775");
    expect_token("LoRa token SF", cmd, 1, "SF", "12");
    expect_token("LoRa token BW", cmd, 2, "BW", "125");
    expect_token("LoRa token GETRSSI", cmd, 3, "GETRSSI", "1");
}

static void test_lowercase_keys(void)
{
    ConfigCommand cmd = config_parse_command(
        "SET mode=fsk br=9.6 freqdev=3.0 sync=0x2dd4\n");

    expect_str("lowercase MODE is uppercased", cmd.mode, "FSK");
    expect_size("lowercase command token count", cmd.tokens.size(), 3);

    expect_token("lowercase token BR", cmd, 0, "BR", "9.6");
    expect_token("lowercase token FREQDEV", cmd, 1, "FREQDEV", "3.0");
    expect_token("lowercase token SYNC value preserved", cmd, 2, "SYNC", "0x2dd4");
}

static void test_malformed_tokens_are_reported(void)
{
    ConfigCommand cmd = config_parse_command(
        "SET MODE=LORA BROKEN NOVALUE= =BAD KEY=ok EMPTY=\n");

    expect_str("malformed command mode", cmd.mode, "LORA");
    expect_size("malformed tokens excluded from normal tokens", cmd.tokens.size(), 1);
    expect_token("valid token survives malformed input", cmd, 0, "KEY", "ok");
    expect_size("malformed token count", cmd.malformed_tokens.size(), 4);
    expect_malformed_token("malformed bare token", cmd, 0, "BROKEN");
    expect_malformed_token("malformed empty value", cmd, 1, "NOVALUE=");
    expect_malformed_token("malformed empty key", cmd, 2, "=BAD");
    expect_malformed_token("malformed trailing empty", cmd, 3, "EMPTY=");
}

/* --- CLI parsing and test sequence --- */

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

    test_unknown_command();
    test_set_without_params();
    test_lora_command();
    test_lowercase_keys();
    test_malformed_tokens_are_reported();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
