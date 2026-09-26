/*
 * Register-polled CAD on SX127x (hardware audit HW-1).
 *
 * RadioLib's SX127x::scanChannel() waits on `while(!digitalRead(DIO0))` and
 * polls DIO1 for the detection. On a board that does not route DIO1 -- the
 * Uputronics expansion board -- CadDetected can never be observed, so every
 * scan returns CHANNEL_FREE, busy channel or not, and listen-before-talk is
 * blind. RadioLib's own SX126x and LR11x0 drivers do not work this way: they
 * wait and then read the latched result register. SX127x is the sole outlier.
 *
 * These tests run the REAL Sx127xDriver against the REAL pinned RadioLib with
 * tests/fakes/sx127x_register_model.h where the chip would be. A fake
 * RadioDriver would prove nothing here: the defect is in the library's
 * register sequence, so the register sequence is what is exercised.
 */

#include "../sx127x_driver.h"
#include "fakes/sx127x_register_model.h"

#include <chrono>
#include <stdio.h>

/* ---- assert helpers ------------------------------------------------------ */

static int g_ok = 0;
static int g_fail = 0;

static void expect(const char *name, bool cond, const char *detail)
{
    if (cond) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: %s\n", name, detail);
    }
}

static void expect_int(const char *name, long got, long want)
{
    char detail[128];
    snprintf(detail, sizeof(detail), "got %ld, want %ld", got, want);
    expect(name, got == want, detail);
}

/* ---- rig ----------------------------------------------------------------- */

/*
 * The pin numbers are the LoRaHAM profile's; the model ignores them, which is
 * the point -- register CAD must not depend on any of them. Nothing here calls
 * begin(): a full boot would exercise a hundred registers that have nothing to
 * do with this repair, so the chip is simply placed in the state begin() leaves
 * it in (LoRa modem, standby).
 */
struct Rig {
    Sx127xRegisterModel model;
    Module mod;
    Sx127xDriver drv;

    Rig() : mod(&model, 8, 25, 5, 24), drv(&mod, false)
    {
        model.reset();
    }
};

/* ---- tests --------------------------------------------------------------- */

/*
 * CadDone alone means the correlation ran and found nothing. The datasheet
 * (p.44) is explicit that a successful correlation asserts CadDetected AND
 * CadDone together, so CadDone without CadDetected is a complete, negative
 * answer -- not an unfinished one.
 */
static void test_cad_done_alone_is_free(void)
{
    Rig rig;
    rig.model.cad_reads_until_done = 0;
    rig.model.cad_detects = false;

    int16_t state = rig.drv.scanChannel();

    expect_int("CadDone alone -> CHANNEL_FREE", state, RADIOLIB_CHANNEL_FREE);
    expect_int("the scan really entered CAD mode", rig.model.cad_entries, 1);
    expect("no pin was consulted for the verdict",
           rig.model.digital_reads == 0,
           "the repair exists so a board without DIO1 can do CAD; a pin read "
           "here means the verdict still depends on wiring");
}

/*
 * The defect, stated positively: a busy channel must come back BUSY. Before
 * the repair this returned CHANNEL_FREE on any board whose DIO1 is not routed.
 */
static void test_cad_detected_is_busy(void)
{
    Rig rig;
    rig.model.cad_reads_until_done = 0;
    rig.model.cad_detects = true;

    int16_t state = rig.drv.scanChannel();

    expect_int("CadDone+CadDetected -> PREAMBLE_DETECTED", state,
               RADIOLIB_PREAMBLE_DETECTED);
}

/*
 * Both bits come out of one register read, so no ordering between them exists
 * to get wrong -- and the clear happens after the verdict is taken, never
 * before. A clear-then-test would discard the detection it was meant to read.
 */
static void test_detection_survives_because_the_clear_comes_after(void)
{
    Rig rig;
    rig.model.cad_reads_until_done = 3;
    rig.model.cad_detects = true;

    int16_t state = rig.drv.scanChannel();

    expect_int("a late detection is still BUSY", state,
               RADIOLIB_PREAMBLE_DETECTED);
    expect_int("the stale flags were cleared once",
               rig.model.irq_flag_clears >= 1, 1);
    expect_int("both CAD bits were cleared",
               rig.model.last_irq_clear_mask &
                   (Sx127xRegisterModel::FLAG_CAD_DONE |
                    Sx127xRegisterModel::FLAG_CAD_DETECTED),
               Sx127xRegisterModel::FLAG_CAD_DONE |
                   Sx127xRegisterModel::FLAG_CAD_DETECTED);
    expect_int("no CAD flag is left standing for the next scan",
               rig.model.peek(Sx127xRegisterModel::REG_IRQ_FLAGS) &
                   (Sx127xRegisterModel::FLAG_CAD_DONE |
                    Sx127xRegisterModel::FLAG_CAD_DETECTED),
               0);
}

/*
 * A chip that never asserts CadDone must not hang the daemon and must not be
 * reported as FREE. The answer is a negative RadioLib error, which
 * radio_cad_status_from_scan_state() maps to UNAVAILABLE -- and
 * daemon_data_tx_runtime.cpp turns UNAVAILABLE into DATA_TX_CAD_WAIT_ERROR,
 * returning at once rather than falling through to CADTXAFTERTIMEOUT. That is
 * the whole reason this is an error and not a BUSY.
 */
static void test_wedged_chip_times_out_indeterminate(void)
{
    Rig rig;
    rig.model.cad_reads_until_done = -1;   /* never completes */

    const auto started = std::chrono::steady_clock::now();
    int16_t state = rig.drv.scanChannel();
    const auto elapsed = std::chrono::steady_clock::now() - started;

    expect("a wedged CAD is neither FREE nor BUSY",
           state != RADIOLIB_CHANNEL_FREE &&
               state != RADIOLIB_PREAMBLE_DETECTED,
           "a timeout reported as a verdict is how a broken scan reaches the "
           "air: BUSY would let CADTXAFTERTIMEOUT send anyway, FREE sends "
           "immediately");
    expect("the timeout is a negative RadioLib error (-> UNAVAILABLE)",
           state < 0 && state != RADIOLIB_CHANNEL_FREE &&
               state != RADIOLIB_PREAMBLE_DETECTED,
           "radio_cad_status_from_scan_state() only maps unrecognised "
           "NEGATIVE states to UNAVAILABLE");
    expect("the wait actually polled the register",
           rig.model.irq_flag_reads > 1,
           "one read and out is not a deadline, it is a coin toss");
    expect("the chip is left in standby, not abandoned in CAD",
           !rig.model.in_cad_mode(),
           "leaving the chip in CAD mode would break the RX re-arm that "
           "follows every probe");

    /* Seeded SF12/BW7.8 is the slowest configuration the validator accepts:
     * (2^12+32)/7800 + 2^12/7800 = 1.05 s upper, deadline 2.1 s. The bound is
     * computed, never a constant -- so this is an order-of-magnitude check,
     * not a stopwatch. */
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    char detail[160];
    snprintf(detail, sizeof(detail),
             "waited %lld ms; the SF12/BW7.8 bound is ~2.1 s", (long long)ms);
    expect("the deadline is computed from SF/BW, not a fixed constant",
           ms > 1500 && ms < 4000, detail);
}

/* ---- the SF/BW the deadline is computed from ----------------------------- */

/*
 * How long a wedged CAD waits IS the observable for the driver's cached SF/BW:
 * the deadline is 2 x ((2^SF + 32)/BW + 2^SF/BW), floor 10 ms, and nothing else
 * reads those two fields. So each case below boots the chip, changes the
 * configuration one way or another, and measures a scan that never completes.
 */
static long wedged_cad_ms(Rig &rig)
{
    rig.model.cad_reads_until_done = -1;

    const auto started = std::chrono::steady_clock::now();
    (void)rig.drv.scanChannel();
    return (long)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
}

static RadioRfDefaults lora_defaults(int sf, float bw_khz)
{
    RadioRfDefaults def{};

    def.freq_mhz = 433.775f;
    def.spreading_factor = sf;
    def.bandwidth_khz = bw_khz;
    def.sync_word = 0x12;
    def.preamble_len = 8;
    def.coding_rate = 5;
    def.crc_on = true;
    def.ldro = -1;
    def.power_dbm = 17;

    return def;
}

static void expect_ms_near(const char *name, long got, long want)
{
    /* +-40 % and a 5 ms allowance: this asserts WHICH configuration the bound
     * was computed from, not the scheduler's accuracy. The candidate values are
     * an order of magnitude apart, so the band cannot confuse them. */
    const long slack = want * 2 / 5 + 5;
    char detail[160];
    snprintf(detail, sizeof(detail), "waited %ld ms, expected about %ld ms",
             got, want);
    expect(name, got >= want - slack && got <= want + slack, detail);
}

/*
 * Boot seeds the cache from the defaults that were actually applied. Before
 * begin() the driver holds the slowest configuration the validator accepts, so
 * a fast boot must visibly shorten the bound -- proving the seed happened
 * rather than the constructor value surviving.
 */
static void test_boot_seeds_the_cache(void)
{
    Rig slow;
    RadioRfDefaults slow_def = lora_defaults(12, 125.0f);
    expect_int("slow boot succeeds", slow.drv.begin(&slow_def),
               RADIOLIB_ERR_NONE);
    /* SF12/BW125: (4096+32)/125000 + 4096/125000 = 65.8 ms upper, x2. */
    expect_ms_near("SF12/BW125 boot -> ~132 ms bound", wedged_cad_ms(slow), 132);

    Rig fast;
    RadioRfDefaults fast_def = lora_defaults(7, 500.0f);
    expect_int("fast boot succeeds", fast.drv.begin(&fast_def),
               RADIOLIB_ERR_NONE);
    /* SF7/BW500: 0.58 ms upper, so the 10 ms floor applies. */
    expect_ms_near("SF7/BW500 boot -> the 10 ms floor", wedged_cad_ms(fast), 10);
}

/* A successful CONFIG SET must move the bound with the chip. */
static void test_successful_set_updates_the_cache(void)
{
    Rig rig;
    RadioRfDefaults def = lora_defaults(7, 500.0f);
    expect_int("boot succeeds", rig.drv.begin(&def), RADIOLIB_ERR_NONE);

    expect_int("SET BW 7.8 accepted",
               rig.drv.applyLoraParam("t", "BW", "7.8"), RADIOLIB_ERR_NONE);
    printf("\n");   /* applyLoraParam prints its coloured fragment inline */

    /* SF7/BW7.8: (128+32)/7800 + 128/7800 = 36.9 ms upper, x2. */
    expect_ms_near("SET BW moved the bound with the chip",
                   wedged_cad_ms(rig), 74);
}

/*
 * A setter the CHIP rejected must not move the cache: the deadline has to
 * describe the configuration the radio is really on, or it is guesswork. The
 * write is dropped at RegModemConfig1, which RadioLib catches through its own
 * read-back verification.
 */
static void test_failed_set_does_not_update_the_cache(void)
{
    Rig rig;
    RadioRfDefaults def = lora_defaults(7, 500.0f);
    expect_int("boot succeeds", rig.drv.begin(&def), RADIOLIB_ERR_NONE);

    rig.model.reject_writes_to = 0x1D;   /* RegModemConfig1 carries BW */
    const int16_t state = rig.drv.applyLoraParam("t", "BW", "7.8");
    printf("\n");
    rig.model.reject_writes_to = -1;

    expect("a rejected SET BW reports the chip's error", state != RADIOLIB_ERR_NONE,
           "the write was dropped, so RadioLib's read-back verification must "
           "have failed");
    expect_ms_near("the bound still describes the chip, not the intent",
                   wedged_cad_ms(rig), 10);
}

/*
 * FSK and back. switchMode() to LoRa lands on the band boot defaults, so the
 * cache must land there too rather than keep whatever the last CONFIG SET said.
 */
static void test_lora_reentry_resets_to_boot_defaults(void)
{
    Rig rig;
    RadioRfDefaults def = lora_defaults(7, 500.0f);
    expect_int("boot succeeds", rig.drv.begin(&def), RADIOLIB_ERR_NONE);

    expect_int("SET BW 7.8 accepted",
               rig.drv.applyLoraParam("t", "BW", "7.8"), RADIOLIB_ERR_NONE);
    printf("\n");
    expect_ms_near("the slow value is in effect", wedged_cad_ms(rig), 74);

    RadioRfDefaults boot = lora_defaults(7, 500.0f);
    expect_int("LoRa re-entry succeeds",
               rig.drv.switchMode(RADIO_MODE_LORA, &boot), RADIOLIB_ERR_NONE);
    expect_ms_near("re-entry put the bound back on the boot defaults",
                   wedged_cad_ms(rig), 10);
}

/*
 * Both profiles, one mechanism. The LoRaHAM board routes DIO1 and the
 * Uputronics board does not; the register verdict is identical and neither
 * reads a pin. That is the difference between a repair and a workaround.
 */
static void test_both_profiles_get_the_same_register_verdict(void)
{
    for (int routed = 0; routed <= 1; routed++) {
        Rig rig;
        rig.model.dio1_routed = (routed != 0);
        rig.model.cad_reads_until_done = 1;
        rig.model.cad_detects = true;

        const int16_t state = rig.drv.scanChannel();
        char name[96];

        snprintf(name, sizeof(name), "%s wiring -> BUSY from the register",
                 routed ? "LoRaHAM (DIO1 routed)" : "Uputronics (no DIO1)");
        expect_int(name, state, RADIOLIB_PREAMBLE_DETECTED);

        snprintf(name, sizeof(name), "%s wiring -> no pin consulted",
                 routed ? "LoRaHAM (DIO1 routed)" : "Uputronics (no DIO1)");
        expect_int(name, rig.model.digital_reads, 0);
    }
}

/* ---- output power and the PA over-current limit -------------------------- */

/*
 * HW-6 + HW-7 are one transmitter invariant, and the register file is where
 * that can be checked.
 *
 * RadioLib pins OCP to 60 mA inside BOTH begin() and beginFSK(), below the
 * datasheet typical draw of 87 mA at +17 dBm on PA_BOOST, and the daemon never
 * set it -- so the protection could trip during ordinary transmission. Because
 * beginFSK() re-pins it, a fix at boot alone would be undone by every
 * LoRa/FSK switch. These tests therefore check the register after boot, after
 * SET POWER, and after a mode switch in both directions.
 *
 * RegOcp (0x0B): bit 5 enables the protection, bits 4:0 are OcpTrim. For
 * 45..120 mA the trim is (mA - 45)/5, so the silicon default 100 mA is trim 11
 * with the enable bit -> 0x2B, and RadioLib's 60 mA would be trim 3 -> 0x23.
 *
 * 100 mA is the chip's own default, chosen over a margin of our own because no
 * current meter was available to justify one. RadioLib's 60 mA is the defect:
 * it sits below the 87 mA the PA typically draws at +17 dBm.
 */
static const uint8_t REG_OCP = 0x0B;
static const uint8_t OCP_DEFAULT_MA = 0x20 | 11;  /* enabled, trim 11 = 100 mA */
static const uint8_t OCP_60_MA  = 0x20 | 3;    /* what RadioLib leaves behind */

static void expect_ocp(const char *name, uint8_t got)
{
    char detail[160];
    snprintf(detail, sizeof(detail),
             "RegOcp = 0x%02X, expected 0x%02X (100 mA, the silicon default); "
             "0x%02X is RadioLib's 60 mA, below the 87 mA the PA draws at "
             "+17 dBm",
             got, OCP_DEFAULT_MA, OCP_60_MA);
    expect(name, got == OCP_DEFAULT_MA, detail);
}

static void test_boot_sets_the_over_current_limit(void)
{
    Rig rig;
    RadioRfDefaults def = lora_defaults(12, 125.0f);

    expect_int("boot succeeds", rig.drv.begin(&def), RADIOLIB_ERR_NONE);
    expect_ocp("boot leaves OCP at the silicon default", rig.model.peek(REG_OCP));
}

static void test_set_power_carries_the_limit_with_it(void)
{
    Rig rig;
    RadioRfDefaults def = lora_defaults(12, 125.0f);

    expect_int("boot succeeds", rig.drv.begin(&def), RADIOLIB_ERR_NONE);

    /* Put the register back where RadioLib would leave it, so the assertion
     * below is about THIS call and not about the boot above. */
    rig.model.poke(REG_OCP, OCP_60_MA);

    expect_int("SET POWER 10 accepted",
               rig.drv.applyLoraParam("t", "POWER", "10"), RADIOLIB_ERR_NONE);
    printf("\n");
    expect_ocp("SET POWER re-applies the limit", rig.model.peek(REG_OCP));
}

/*
 * The one that a boot-only fix would have missed. beginFSK() re-pins OCP to
 * 60 mA and resets the output power, so every LoRa/FSK switch silently undid
 * the setting.
 */
static void test_mode_switches_reapply_the_limit(void)
{
    Rig rig;
    RadioRfDefaults def = lora_defaults(12, 125.0f);

    expect_int("boot succeeds", rig.drv.begin(&def), RADIOLIB_ERR_NONE);

    /* Put the register where RadioLib would leave it FIRST, or this passes on
     * the value boot already wrote and proves nothing about the switch. */
    rig.model.poke(REG_OCP, OCP_60_MA);
    expect_int("switch to FSK succeeds",
               rig.drv.switchMode(RADIO_MODE_FSK, &def), RADIOLIB_ERR_NONE);
    expect_ocp("FSK switch re-applies the limit", rig.model.peek(REG_OCP));

    rig.model.poke(REG_OCP, OCP_60_MA);
    expect_int("switch back to LoRa succeeds",
               rig.drv.switchMode(RADIO_MODE_LORA, &def), RADIOLIB_ERR_NONE);
    expect_ocp("LoRa re-entry re-applies the limit", rig.model.peek(REG_OCP));
}

/* The SX127x power range the validator now enforces, applied for real. */
static void test_the_sx127x_power_range_applies(void)
{
    Rig rig;
    RadioRfDefaults def = lora_defaults(12, 125.0f);

    expect_int("boot succeeds", rig.drv.begin(&def), RADIOLIB_ERR_NONE);

    expect_int("SET POWER 2 accepted (lowest PA_BOOST step)",
               rig.drv.applyLoraParam("t", "POWER", "2"), RADIOLIB_ERR_NONE);
    printf("\n");
    expect_int("SET POWER 17 accepted (continuous maximum)",
               rig.drv.applyLoraParam("t", "POWER", "17"), RADIOLIB_ERR_NONE);
    printf("\n");

    /* Rejected before the chip: the driver reports 0 (no hardware error) and
     * prints the value as refused, and the register must not have moved. */
    rig.model.poke(REG_OCP, 0x00);
    expect_int("SET POWER 20 does not reach the chip",
               rig.drv.applyLoraParam("t", "POWER", "20"), 0);
    printf("\n");
    expect_int("SET POWER 20 wrote nothing", rig.model.peek(REG_OCP), 0);

    expect_int("SET POWER 0 does not reach the chip",
               rig.drv.applyLoraParam("t", "POWER", "0"), 0);
    printf("\n");
    expect_int("SET POWER 0 wrote nothing", rig.model.peek(REG_OCP), 0);
}

/* ---- LDRO ---------------------------------------------------------------- */

/*
 * HW-4. RadioLib's autoLDRO() sets a flag and writes NO register -- verified
 * above in SX1278.cpp, which does exactly `this->ldroAuto = true; return OK;`.
 * RadioLib then only writes on a cache difference, so on a board with no RESET
 * line (Uputronics) a previously forced LDRO bit survives the restart in the
 * chip while the cache assumes the power-on state.
 *
 * The boot path already worked around this, with a comment naming a
 * bench-verified corrupt decode at SF11/BW250. `SET LDRO=AUTO` did not: it
 * called autoLDRO() alone and reported success while the stale bit stayed in
 * the register.
 *
 * RegModemConfig3 (0x26) bit 3 is LowDataRateOptimize.
 */
static const uint8_t REG_MODEM_CONFIG_3 = 0x26;
static const uint8_t LDRO_BIT = 0x08;

static bool ldro_set(Rig &rig)
{
    return (rig.model.peek(REG_MODEM_CONFIG_3) & LDRO_BIT) != 0;
}

/*
 * The boundary, at boot. LDRO is mandated from a symbol time of 16 ms:
 * SF12/BW125 is 32.8 ms and needs it, SF7/BW500 is 0.26 ms and must not have
 * it -- LDRO on a fast configuration costs payload symbols for nothing.
 */
static void test_boot_writes_the_ldro_bit_for_the_configuration(void)
{
    Rig slow;
    RadioRfDefaults slow_def = lora_defaults(12, 125.0f);
    expect_int("slow boot succeeds", slow.drv.begin(&slow_def),
               RADIOLIB_ERR_NONE);
    expect("SF12/BW125 (32.8 ms symbol) boots with LDRO on", ldro_set(slow),
           "a long symbol without LDRO decodes corruptly as the crystal drifts");

    Rig fast;
    RadioRfDefaults fast_def = lora_defaults(7, 500.0f);
    expect_int("fast boot succeeds", fast.drv.begin(&fast_def),
               RADIOLIB_ERR_NONE);
    expect("SF7/BW500 (0.26 ms symbol) boots with LDRO off", !ldro_set(fast),
           "LDRO on a fast configuration spends payload symbols for nothing");
}

/*
 * The defect itself: a stale bit in the chip and a SET LDRO=AUTO that must
 * clear it. This is the no-RESET warm start, modelled exactly -- the register
 * says one thing and RadioLib's cache says another.
 */
static void test_set_ldro_auto_writes_the_register(void)
{
    Rig rig;
    RadioRfDefaults def = lora_defaults(7, 500.0f);

    expect_int("boot succeeds", rig.drv.begin(&def), RADIOLIB_ERR_NONE);

    /* The ghost: a forced bit left in the chip by a previous run. */
    rig.model.poke(REG_MODEM_CONFIG_3,
                   (uint8_t)(rig.model.peek(REG_MODEM_CONFIG_3) | LDRO_BIT));
    expect("the stale bit is in place for the test", ldro_set(rig),
           "the test did not set up the condition it claims to test");

    expect_int("SET LDRO=AUTO accepted",
               rig.drv.applyLoraParam("t", "LDRO", "AUTO"), RADIOLIB_ERR_NONE);
    printf("\n");

    expect("SET LDRO=AUTO cleared the stale bit", !ldro_set(rig),
           "autoLDRO() alone sets a flag and writes nothing, so the register "
           "keeps the previous run's value while the command reports success");
}

/*
 * AUTO must also STAY automatic. forceLDRO() clears RadioLib's ldroAuto flag
 * permanently, so the helper has to call it before autoLDRO(), not after -- get
 * that order wrong and LDRO is correct once and then frozen.
 */
static void test_auto_keeps_tracking_later_sf_changes(void)
{
    Rig rig;
    RadioRfDefaults def = lora_defaults(7, 500.0f);

    expect_int("boot succeeds", rig.drv.begin(&def), RADIOLIB_ERR_NONE);
    expect_int("SET LDRO=AUTO accepted",
               rig.drv.applyLoraParam("t", "LDRO", "AUTO"), RADIOLIB_ERR_NONE);
    printf("\n");
    expect("fast configuration starts without LDRO", !ldro_set(rig), "");

    /* SF12 at BW500 is 8.2 ms -- still below the boundary. */
    expect_int("SET SF=12 accepted",
               rig.drv.applyLoraParam("t", "SF", "12"), RADIOLIB_ERR_NONE);
    printf("\n");
    expect("SF12/BW500 (8.2 ms symbol) still needs no LDRO", !ldro_set(rig),
           "the boundary is the symbol time, not the spreading factor");

    /* Narrowing to 62.5 kHz makes it 65.5 ms, well past the boundary. */
    expect_int("SET BW=62.5 accepted",
               rig.drv.applyLoraParam("t", "BW", "62.5"), RADIOLIB_ERR_NONE);
    printf("\n");
    expect("a later BW change turned LDRO on by itself", ldro_set(rig),
           "AUTO stopped tracking: forceLDRO() must be called BEFORE "
           "autoLDRO(), or it clears the auto flag again");
}

/* An explicit value stays explicit -- AUTO is opt-in, not a hijack. */
static void test_explicit_ldro_is_not_overridden(void)
{
    Rig rig;
    RadioRfDefaults def = lora_defaults(12, 125.0f);   /* would need LDRO */

    expect_int("boot succeeds", rig.drv.begin(&def), RADIOLIB_ERR_NONE);
    expect("boot turned LDRO on for SF12/BW125", ldro_set(rig), "");

    expect_int("SET LDRO=0 accepted",
               rig.drv.applyLoraParam("t", "LDRO", "0"), RADIOLIB_ERR_NONE);
    printf("\n");
    expect("an explicit 0 turns the bit off even where AUTO would set it",
           !ldro_set(rig), "explicit means explicit");
}

/* ---- TX must never report success off a dead IRQ line -------------------- */

/*
 * Harness contract item 2. RadioLib's SX127x::transmit() waits
 * `while(!digitalRead(irq))`, and before the HAL repair a GPIO read error came
 * back through uint32_t as a large positive number -- so the condition was
 * false immediately, the loop never ran once, and control fell straight into
 * finishTransmit(), which clears the flags, goes to standby and returns
 * ERR_NONE. The daemon cut the transmission short on air and logged the frame
 * as sent.
 *
 * The HAL repair answers LOW instead, and these two tests are the other half of
 * that argument: LOW is only safe because the wait is BOUNDED. It is -- 150 %
 * of the computed time-on-air in LoRa -- so a DIO0 that never asserts produces
 * ERR_TX_TIMEOUT, never ERR_NONE. Checked here against the pinned library
 * rather than asserted in a comment.
 */
static void test_transmit_times_out_when_dio0_never_asserts(void)
{
    Rig rig;
    RadioRfDefaults def = lora_defaults(7, 500.0f);   /* short time-on-air */
    uint8_t payload[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };

    expect_int("boot succeeds", rig.drv.begin(&def), RADIOLIB_ERR_NONE);

    rig.model.tx_completes = false;   /* the line never rises */
    const int16_t state = rig.drv.transmit(payload, sizeof(payload));

    expect("a transmission with a dead IRQ line is not reported as sent",
           state != RADIOLIB_ERR_NONE,
           "this is how a cut-short transmission gets logged as successful");
    expect_int("it is specifically a timeout", state, RADIOLIB_ERR_TX_TIMEOUT);
    expect_int("the chip really was put into TX", rig.model.tx_entries, 1);
    expect("the chip is left in standby afterwards",
           (rig.model.peek(Sx127xRegisterModel::REG_OP_MODE) &
            Sx127xRegisterModel::MODE_MASK) ==
               Sx127xRegisterModel::MODE_STANDBY,
           "finishTransmit() must still park the transmitter");
}

/* The positive control: the same path succeeds when the line does assert, so
 * the test above is about the dead line and not about the rig. */
static void test_transmit_succeeds_when_dio0_asserts(void)
{
    Rig rig;
    RadioRfDefaults def = lora_defaults(7, 500.0f);
    uint8_t payload[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };

    expect_int("boot succeeds", rig.drv.begin(&def), RADIOLIB_ERR_NONE);

    rig.model.tx_completes = true;
    expect_int("a normal transmission reports success",
               rig.drv.transmit(payload, sizeof(payload)), RADIOLIB_ERR_NONE);
    expect_int("the chip really was put into TX", rig.model.tx_entries, 1);
}

int main(void)
{
    test_cad_done_alone_is_free();
    test_cad_detected_is_busy();
    test_detection_survives_because_the_clear_comes_after();
    test_wedged_chip_times_out_indeterminate();
    test_both_profiles_get_the_same_register_verdict();
    test_boot_seeds_the_cache();
    test_successful_set_updates_the_cache();
    test_failed_set_does_not_update_the_cache();
    test_lora_reentry_resets_to_boot_defaults();
    test_boot_sets_the_over_current_limit();
    test_set_power_carries_the_limit_with_it();
    test_mode_switches_reapply_the_limit();
    test_the_sx127x_power_range_applies();
    test_boot_writes_the_ldro_bit_for_the_configuration();
    test_set_ldro_auto_writes_the_register();
    test_auto_keeps_tracking_later_sf_changes();
    test_explicit_ldro_is_not_overridden();
    test_transmit_times_out_when_dio0_never_asserts();
    test_transmit_succeeds_when_dio0_asserts();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
