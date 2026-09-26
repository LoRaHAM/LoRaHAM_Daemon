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
    expect_int("lora preamble accepts 512", config_policy_lora_preamble_valid(512), 1);
    expect_int("lora preamble rejects 513 (airtime cap)", config_policy_lora_preamble_valid(513), 0);
    expect_int("lora preamble rejects 65535 (airtime cap)", config_policy_lora_preamble_valid(65535), 0);

    expect_int("lora sync accepts ff", config_policy_lora_sync_valid(0xFF), 1);
    expect_int("lora sync rejects 100", config_policy_lora_sync_valid(0x100), 0);

    expect_int("power accepts 0", config_policy_power_valid(0), 1);
    expect_int("power accepts 20", config_policy_power_valid(20), 1);
    expect_int("power rejects -1", config_policy_power_valid(-1), 0);
    expect_int("power rejects 21", config_policy_power_valid(21), 0);
}

/*
 * HW-6: output power is family-specific in both directions, and the family-
 * blind rule above was wrong on both ends for SX127x.
 *
 * Low end: RadioLib's SX1278::setOutputPower routes 2..17 to PA_BOOST and
 * anything below 2 to the RFO pin -- a different output path, and not the one
 * the antenna is on. POWER=0 did not mean "transmit quietly", it meant
 * "transmit into an unconnected pin", and the caller was told it worked.
 *
 * High end: RadioLib's checkOutputPower accepts 2..17 on PA_BOOST and
 * special-cases exactly 20, so 18 and 19 are rejected by the library itself and
 * were never reachable -- rejecting them here only makes the error early and
 * specific. 20 was reachable, via the PA_DAC-boosted path, and is dropped
 * deliberately: the datasheet restricts +20 dBm to duty cycle <= 1 %, VSWR
 * <= 3:1 and VDD 2.4-3.7 V, and this daemon has no duty-cycle governor.
 *
 * SX1262 has one output path and no such restriction, so it keeps 0..20.
 */
/*
 * HW-4: the LDRO boundary, in one place. It lived in three -- the driver's boot
 * path, RadioLib, and the airtime gate -- and the airtime gate used a strict
 * `>` where the other two used `>=`. No SF/BW combination the validator accepts
 * lands on exactly 16.000 ms, so nothing ever differed on air; the point of
 * pinning it is that extracting a helper with an ambiguous boundary would only
 * have moved the disagreement.
 */
static void test_ldro_boundary(void)
{
    /* SF12/BW125 = 32.8 ms; SF11/BW250 = 8.2 ms; SF12/BW250 = 16.384 ms, the
     * closest the accepted raster comes to the boundary from above. */
    expect_int("ldro required at SF12/BW125 (32.8 ms)",
               config_policy_lora_ldro_required(12, 125.0f), 1);
    expect_int("ldro required at SF12/BW250 (16.4 ms)",
               config_policy_lora_ldro_required(12, 250.0f), 1);
    expect_int("ldro not required at SF11/BW250 (8.2 ms)",
               config_policy_lora_ldro_required(11, 250.0f), 0);
    expect_int("ldro not required at SF7/BW500 (0.26 ms)",
               config_policy_lora_ldro_required(7, 500.0f), 0);

    /* The boundary itself is inclusive: 16 ms needs LDRO. SF12/BW256 is exactly
     * 16.000 ms -- not a value the validator accepts, which is precisely why
     * the two spellings never differed on air, and why the rule is pinned here
     * instead of being left to a future reader to rediscover. */
    expect_int("the 16 ms boundary is inclusive",
               config_policy_lora_ldro_required(12, 256.0f), 1);
    expect_int("just above the boundary does not require it",
               config_policy_lora_ldro_required(12, 256.001f), 0);

    /* Nonsense in, false out -- never a divide by zero. */
    expect_int("zero bandwidth is not a reason to enable ldro",
               config_policy_lora_ldro_required(12, 0.0f), 0);
}

static void test_power_policy_per_family(void)
{
    /* Without the --high-power permission: 1.0.0 behaviour, unchanged. */
    expect_int("sx127x rejects 0 (RFO path, not the antenna)",
               config_policy_power_valid_family(0, DAEMON_CHIP_FAMILY_SX127X, false), 0);
    expect_int("sx127x rejects 1 (RFO path, not the antenna)",
               config_policy_power_valid_family(1, DAEMON_CHIP_FAMILY_SX127X, false), 0);
    expect_int("sx127x accepts 2 (lowest PA_BOOST step)",
               config_policy_power_valid_family(2, DAEMON_CHIP_FAMILY_SX127X, false), 1);
    expect_int("sx127x accepts 17 (continuous-operation maximum)",
               config_policy_power_valid_family(17, DAEMON_CHIP_FAMILY_SX127X, false), 1);
    expect_int("sx127x rejects 18 (RadioLib API boundary; early beats late)",
               config_policy_power_valid_family(18, DAEMON_CHIP_FAMILY_SX127X, false), 0);
    expect_int("sx127x rejects 19 (RadioLib API boundary; early beats late)",
               config_policy_power_valid_family(19, DAEMON_CHIP_FAMILY_SX127X, false), 0);
    expect_int("sx127x rejects 20 without the boot permission",
               config_policy_power_valid_family(20, DAEMON_CHIP_FAMILY_SX127X, false), 0);
    expect_int("sx127x rejects -1",
               config_policy_power_valid_family(-1, DAEMON_CHIP_FAMILY_SX127X, false), 0);

    /* With the permission: exactly 20 joins the set, nothing else moves. */
    expect_int("sx127x with permission accepts 20",
               config_policy_power_valid_family(20, DAEMON_CHIP_FAMILY_SX127X, true), 1);
    expect_int("sx127x with permission still accepts 17",
               config_policy_power_valid_family(17, DAEMON_CHIP_FAMILY_SX127X, true), 1);
    expect_int("sx127x with permission still accepts 2",
               config_policy_power_valid_family(2, DAEMON_CHIP_FAMILY_SX127X, true), 1);
    expect_int("sx127x with permission still rejects 18",
               config_policy_power_valid_family(18, DAEMON_CHIP_FAMILY_SX127X, true), 0);
    expect_int("sx127x with permission still rejects 19",
               config_policy_power_valid_family(19, DAEMON_CHIP_FAMILY_SX127X, true), 0);
    expect_int("sx127x with permission still rejects 21",
               config_policy_power_valid_family(21, DAEMON_CHIP_FAMILY_SX127X, true), 0);
    expect_int("sx127x with permission still rejects 0 (RFO path)",
               config_policy_power_valid_family(0, DAEMON_CHIP_FAMILY_SX127X, true), 0);
    expect_int("sx127x with permission still rejects 1 (RFO path)",
               config_policy_power_valid_family(1, DAEMON_CHIP_FAMILY_SX127X, true), 0);

    /* SX1262: the permission is irrelevant in both directions. */
    expect_int("sx1262 keeps 0",
               config_policy_power_valid_family(0, DAEMON_CHIP_FAMILY_SX1262, false), 1);
    expect_int("sx1262 keeps 20 without permission",
               config_policy_power_valid_family(20, DAEMON_CHIP_FAMILY_SX1262, false), 1);
    expect_int("sx1262 keeps 20 with permission",
               config_policy_power_valid_family(20, DAEMON_CHIP_FAMILY_SX1262, true), 1);
    expect_int("sx1262 rejects 21",
               config_policy_power_valid_family(21, DAEMON_CHIP_FAMILY_SX1262, false), 0);
    expect_int("sx1262 rejects 21 with permission too",
               config_policy_power_valid_family(21, DAEMON_CHIP_FAMILY_SX1262, true), 0);
    expect_int("sx1262 rejects -1",
               config_policy_power_valid_family(-1, DAEMON_CHIP_FAMILY_SX1262, false), 0);
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

    // SX126x raster (RadioLib SX126x::setRxBandwidth) + family selector.
    expect_int("fsk rxbw sx126x accepts 4.8",
               config_policy_fsk_rxbw_valid_sx126x(4.8f), 1);
    expect_int("fsk rxbw sx126x accepts 23.4",
               config_policy_fsk_rxbw_valid_sx126x(23.4f), 1);
    expect_int("fsk rxbw sx126x accepts 467",
               config_policy_fsk_rxbw_valid_sx126x(467.0f), 1);
    expect_int("fsk rxbw sx126x rejects 20.8",
               config_policy_fsk_rxbw_valid_sx126x(20.8f), 0);
    expect_int("fsk rxbw sx126x rejects 25.0",
               config_policy_fsk_rxbw_valid_sx126x(25.0f), 0);
    expect_int("fsk rxbw family sx127x accepts 20.8",
               config_policy_fsk_rxbw_valid_family(20.8f,
                                                   DAEMON_CHIP_FAMILY_SX127X), 1);
    expect_int("fsk rxbw family sx127x rejects 23.4",
               config_policy_fsk_rxbw_valid_family(23.4f,
                                                   DAEMON_CHIP_FAMILY_SX127X), 0);
    expect_int("fsk rxbw family sx1262 accepts 23.4",
               config_policy_fsk_rxbw_valid_family(23.4f,
                                                   DAEMON_CHIP_FAMILY_SX1262), 1);
    expect_int("fsk rxbw family sx1262 rejects 25.0",
               config_policy_fsk_rxbw_valid_family(25.0f,
                                                   DAEMON_CHIP_FAMILY_SX1262), 0);

    expect_int("fsk preamble accepts 0", config_policy_fsk_preamble_valid(0), 1);
    expect_int("fsk preamble rejects -1", config_policy_fsk_preamble_valid(-1), 0);
    expect_int("fsk preamble accepts 2048", config_policy_fsk_preamble_valid(2048), 1);
    expect_int("fsk preamble rejects 2049 (airtime cap)", config_policy_fsk_preamble_valid(2049), 0);

    expect_int("fsk sync rejects 0", config_policy_fsk_sync_valid(0x00), 0);
    expect_int("fsk sync accepts 12", config_policy_fsk_sync_valid(0x12), 1);
    expect_int("fsk sync accepts 12ad", config_policy_fsk_sync_valid(0x12AD), 1);
    expect_int("fsk sync rejects 1200", config_policy_fsk_sync_valid(0x1200), 0);
    expect_int("fsk sync accepts normalized 0012", config_policy_fsk_sync_valid(0x0012), 1);
    expect_int("fsk sync rejects 10000", config_policy_fsk_sync_valid(0x10000), 0);
}

/* Audit P1-4: band frequency policy. */
static void test_freq_band_policy(void)
{
    expect_int("433 band accepts 433.900",
               config_policy_freq_valid_band(433.900f, 430.0f, 440.0f), 1);
    expect_int("433 band rejects 869.525",
               config_policy_freq_valid_band(869.525f, 430.0f, 440.0f), 0);
    expect_int("868 band accepts 869.525",
               config_policy_freq_valid_band(869.525f, 863.0f, 870.0f), 1);
    expect_int("868 band rejects 433.900",
               config_policy_freq_valid_band(433.900f, 863.0f, 870.0f), 0);
    expect_int("band edges inclusive",
               config_policy_freq_valid_band(430.0f, 430.0f, 440.0f), 1);
    expect_int("degenerate range fails closed",
               config_policy_freq_valid_band(433.9f, 0.0f, 0.0f), 0);
}

/* Audit P1-3: family capability policy — prevalidation matches the driver. */
static void test_family_capabilities(void)
{
    expect_int("sx127x freqdev 0.5 ok",
               config_policy_fsk_freqdev_valid_family(0.5f, DAEMON_CHIP_FAMILY_SX127X), 1);
    expect_int("sx1262 freqdev 0.5 rejected",
               config_policy_fsk_freqdev_valid_family(0.5f, DAEMON_CHIP_FAMILY_SX1262), 0);
    expect_int("sx1262 freqdev 0.6 ok",
               config_policy_fsk_freqdev_valid_family(0.6f, DAEMON_CHIP_FAMILY_SX1262), 1);
    expect_int("sx127x OOK=1 ok",
               config_policy_fsk_ook_valid_family(1, DAEMON_CHIP_FAMILY_SX127X), 1);
    expect_int("sx1262 OOK=1 rejected",
               config_policy_fsk_ook_valid_family(1, DAEMON_CHIP_FAMILY_SX1262), 0);
    expect_int("sx1262 OOK=0 also rejected (no OOK capability)",
               config_policy_fsk_ook_valid_family(0, DAEMON_CHIP_FAMILY_SX1262), 0);
    expect_int("sx127x ENCODING=1 ok",
               config_policy_fsk_encoding_valid_family(1, DAEMON_CHIP_FAMILY_SX127X), 1);
    expect_int("sx1262 ENCODING=1 rejected (whitening alias)",
               config_policy_fsk_encoding_valid_family(1, DAEMON_CHIP_FAMILY_SX1262), 0);
    expect_int("sx1262 ENCODING=2 ok",
               config_policy_fsk_encoding_valid_family(2, DAEMON_CHIP_FAMILY_SX1262), 1);
}

/* Audit P1-7: airtime calculator reference points. */
static void expect_between(const char *name, double v, double lo, double hi)
{
    if (v >= lo && v <= hi) {
        g_ok++;
        printf("[ OK ] %s (%.1f ms)\n", name, v);
    } else {
        g_fail++;
        printf("[FAIL] %s: %.1f not in [%.1f, %.1f]\n", name, v, lo, hi);
    }
}

static void test_airtime_reference_points(void)
{
    /* 433 boot profile, 255 B: ~9.0 s — must pass the 20 s limit. */
    expect_between("airtime SF12/BW125/CR5/PRE8",
                   config_policy_lora_airtime_ms(12, 125.0f, 5, 8, 255),
                   8900.0, 9150.0);
    /* 868 boot profile: ~2.2 s. */
    expect_between("airtime SF11/BW250/CR5/PRE16",
                   config_policy_lora_airtime_ms(11, 250.0f, 5, 16, 255),
                   2100.0, 2250.0);
    /* The audit's example: SF12/BW7.8/CR8/PRE512 ≈ 489 s. */
    expect_between("airtime audit worst case",
                   config_policy_lora_airtime_ms(12, 7.8f, 8, 512, 255),
                   480000.0, 500000.0),
    expect_int("audit worst case over limit",
               config_policy_lora_airtime_ms(12, 7.8f, 8, 512, 255)
                   > CONFIG_POLICY_MAX_AIRTIME_MS, 1);
    /* FSK worst case (BR 0.5, preamble 2048): ~8.3 s — under the limit. */
    expect_between("airtime FSK worst case",
                   config_policy_fsk_airtime_ms(0.5f, 2048, 255),
                   8200.0, 8500.0);
    expect_int("invalid params fail closed",
               config_policy_lora_airtime_ms(0, 125.0f, 5, 8, 255) < 0.0, 1);
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
    test_ldro_boundary();
    test_power_policy_per_family();
    test_fsk_policy();

    test_freq_band_policy();
    test_family_capabilities();
    test_airtime_reference_points();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
