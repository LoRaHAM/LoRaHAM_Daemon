#include "../config_policy.h"

#include <stdio.h>
#include <string.h>

/*
 * CONFIG policy tests.
 *
 * These tests lock the daemon-facing value contract. They intentionally
 * follow the documented daemon interface where it is narrower than RadioLib.
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

static void test_lora_policy(void)
{
    expect_int("lora sf rejects 6", config_policy_lora_sf_valid(6), 0);
    expect_int("lora sf accepts 7", config_policy_lora_sf_valid(7), 1);
    expect_int("lora sf accepts 12", config_policy_lora_sf_valid(12), 1);
    expect_int("lora sf rejects 13", config_policy_lora_sf_valid(13), 0);

    expect_int("lora bw accepts 125", config_policy_lora_bandwidth_valid(125.0f), 1);
    expect_int("lora bw accepts 31.25", config_policy_lora_bandwidth_valid(31.25f), 1);
    expect_int("lora bw rejects 200", config_policy_lora_bandwidth_valid(200.0f), 0);

    expect_int("lora cr rejects 4", config_policy_lora_cr_valid(4), 0);
    expect_int("lora cr accepts 5", config_policy_lora_cr_valid(5), 1);
    expect_int("lora cr accepts 8", config_policy_lora_cr_valid(8), 1);
    expect_int("lora cr rejects 9", config_policy_lora_cr_valid(9), 0);

    expect_int("lora preamble rejects 5", config_policy_lora_preamble_valid(5), 0);
    expect_int("lora preamble accepts 6", config_policy_lora_preamble_valid(6), 1);
    expect_int("lora preamble accepts 65535", config_policy_lora_preamble_valid(65535), 1);
    expect_int("lora preamble rejects 65536", config_policy_lora_preamble_valid(65536), 0);

    expect_int("lora sync accepts ff", config_policy_lora_sync_valid(0xFF), 1);
    expect_int("lora sync rejects 100", config_policy_lora_sync_valid(0x100), 0);

    expect_int("power accepts 0", config_policy_power_valid(0), 1);
    expect_int("power accepts 20", config_policy_power_valid(20), 1);
    expect_int("power rejects -1", config_policy_power_valid(-1), 0);
    expect_int("power rejects 21", config_policy_power_valid(21), 0);
}

static void test_fsk_policy(void)
{
    expect_int("fsk br rejects 0.49", config_policy_fsk_bitrate_valid(0.49f), 0);
    expect_int("fsk br accepts 0.5", config_policy_fsk_bitrate_valid(0.5f), 1);
    expect_int("fsk br accepts 300", config_policy_fsk_bitrate_valid(300.0f), 1);
    expect_int("fsk br rejects 300.1", config_policy_fsk_bitrate_valid(300.1f), 0);

    expect_int("fsk freqdev rejects 0", config_policy_fsk_freqdev_valid(0.0f), 0);
    expect_int("fsk freqdev accepts 5", config_policy_fsk_freqdev_valid(5.0f), 1);
    expect_int("fsk freqdev accepts 200", config_policy_fsk_freqdev_valid(200.0f), 1);
    expect_int("fsk freqdev rejects 200.1", config_policy_fsk_freqdev_valid(200.1f), 0);

    expect_int("fsk rxbw accepts 2.6", config_policy_fsk_rxbw_valid(2.6f), 1);
    expect_int("fsk rxbw accepts 31.3", config_policy_fsk_rxbw_valid(31.3f), 1);
    expect_int("fsk rxbw accepts 31.25", config_policy_fsk_rxbw_valid(31.25f), 1);
    expect_int("fsk rxbw accepts 250", config_policy_fsk_rxbw_valid(250.0f), 1);
    expect_int("fsk rxbw rejects 30", config_policy_fsk_rxbw_valid(30.0f), 0);

    expect_int("fsk preamble accepts 0", config_policy_fsk_preamble_valid(0), 1);
    expect_int("fsk preamble rejects -1", config_policy_fsk_preamble_valid(-1), 0);

    expect_int("fsk sync rejects 0", config_policy_fsk_sync_valid(0x00), 0);
    expect_int("fsk sync accepts 12", config_policy_fsk_sync_valid(0x12), 1);
    expect_int("fsk sync accepts 12ad", config_policy_fsk_sync_valid(0x12AD), 1);
    expect_int("fsk sync rejects 1200", config_policy_fsk_sync_valid(0x1200), 0);
    expect_int("fsk sync accepts normalized 0012", config_policy_fsk_sync_valid(0x0012), 1);
    expect_int("fsk sync rejects 10000", config_policy_fsk_sync_valid(0x10000), 0);
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

    test_lora_policy();
    test_fsk_policy();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
