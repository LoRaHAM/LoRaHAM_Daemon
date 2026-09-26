/*
 * POWER=20 on SX127x: the register pair, the permission, and the way back.
 *
 * +20 dBm on PA_BOOST is RegPaDac 0x87 (datasheet 5.4.3), and the datasheet
 * says in the same section that the over-current limit "should be adapted to
 * the actual power level". RadioLib's setOutputPower(20) flips PA_DAC and
 * leaves OCP alone; the daemon's applyPowerAndOcp() pairs the two -- 140 mA
 * (OcpTrim 17) at exactly 20, 100 mA (OcpTrim 11) below it -- and the pair
 * has to come back on every path that leaves 20: a lower SET, a MODE switch
 * that reloads the boot defaults, and a boot over a previously boosted chip.
 *
 * These tests run the REAL Sx127xDriver against the REAL pinned RadioLib with
 * tests/fakes/sx127x_register_model.h where the chip would be, exactly like
 * test_sx127x_cad_register.cpp, because the claim is about the register
 * sequence and a fake driver would prove nothing about it.
 *
 * Bytes, not bits: RadioLib writes PA_DAC bits 2:0 and OCP bits 5:0 and
 * preserves the rest, so the model is seeded with the documented ordinary
 * bytes (PA_DAC 0x84) and the whole byte is compared -- 0x87 / 0x84 as the
 * datasheet prints them -- which also proves the upper bits survive.
 */

#include "../sx127x_driver.h"
#include "fakes/sx127x_register_model.h"

#include <stdio.h>

/* ---- assert helpers ------------------------------------------------------ */

static int g_ok = 0;
static int g_fail = 0;

static void expect_hex(const char *name, unsigned got, unsigned want)
{
    if (got == want) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: expected 0x%02X, got 0x%02X\n", name, want, got);
    }
}

static void expect_int(const char *name, long got, long want)
{
    if (got == want) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: expected %ld, got %ld\n", name, want, got);
    }
}

/* ---- the chip, as the datasheet numbers it ------------------------------- */

static const uint8_t REG_PA_CONFIG = 0x09;
static const uint8_t REG_OCP       = 0x0B;
static const uint8_t REG_PA_DAC    = 0x4D;

static const uint8_t PA_DAC_ORDINARY = 0x84;   /* datasheet default byte */
static const uint8_t PA_DAC_BOOST    = 0x87;   /* +20 dBm on PA_BOOST */
static const uint8_t OCP_100MA       = 0x20 | 11;   /* OcpOn | trim 11 = 100 mA */
static const uint8_t OCP_140MA       = 0x20 | 17;   /* OcpOn | trim 17 = 140 mA */

/* ---- rig ----------------------------------------------------------------- */

struct Rig {
    Sx127xRegisterModel model;
    Module mod;
    Sx127xDriver drv;

    explicit Rig(bool high_power) : mod(&model, 8, 25, 5, 24), drv(&mod, false, high_power)
    {
        model.reset();
        /* The documented ordinary byte, so whole-byte comparisons below also
         * prove that RadioLib leaves bits 7:3 alone. */
        model.poke(REG_PA_DAC, PA_DAC_ORDINARY);
    }
};

static RadioRfDefaults lora_defaults(void)
{
    RadioRfDefaults def{};

    def.freq_mhz = 433.775f;
    def.spreading_factor = 7;
    def.bandwidth_khz = 125.0f;
    def.sync_word = 0x12;
    def.preamble_len = 8;
    def.coding_rate = 5;
    def.crc_on = true;
    def.ldro = -1;
    def.power_dbm = 17;

    return def;
}

static void nl(void) { printf("\n"); }   /* apply*Param print inline fragments */

/* ---- tests --------------------------------------------------------------- */

/* Boot over a chip that a previous process left boosted (no RESET on the
 * Uputronics wiring): the ordinary pair must come back without anyone asking. */
static void test_boot_restores_the_ordinary_pair(void)
{
    Rig rig(true);
    RadioRfDefaults def = lora_defaults();

    rig.model.poke(REG_PA_DAC, PA_DAC_BOOST);
    rig.model.poke(REG_OCP, OCP_140MA);

    expect_int("boot succeeds", rig.drv.begin(&def), RADIOLIB_ERR_NONE);
    expect_hex("boot: PA_DAC back to 0x84", rig.model.peek(REG_PA_DAC), PA_DAC_ORDINARY);
    expect_hex("boot: OCP back to 100 mA", rig.model.peek(REG_OCP), OCP_100MA);
    expect_hex("boot: PA_BOOST selected for 17",
               rig.model.peek(REG_PA_CONFIG) & 0x80u, 0x80u);
}

/* Permission ON: 20 writes the pair; 17 afterwards restores both. */
static void test_set_20_then_17_in_lora(void)
{
    Rig rig(true);
    RadioRfDefaults def = lora_defaults();
    expect_int("boot succeeds", rig.drv.begin(&def), RADIOLIB_ERR_NONE);

    expect_int("SET POWER=20 accepted with permission",
               rig.drv.applyLoraParam("t", "POWER", "20"), RADIOLIB_ERR_NONE);
    nl();
    expect_hex("20: PA_DAC is the boosted byte 0x87", rig.model.peek(REG_PA_DAC), PA_DAC_BOOST);
    expect_hex("20: OCP is 140 mA in the same apply", rig.model.peek(REG_OCP), OCP_140MA);
    expect_hex("20: PA_BOOST still selected",
               rig.model.peek(REG_PA_CONFIG) & 0x80u, 0x80u);

    expect_int("SET POWER=17 accepted",
               rig.drv.applyLoraParam("t", "POWER", "17"), RADIOLIB_ERR_NONE);
    nl();
    expect_hex("17 after 20: PA_DAC back to 0x84", rig.model.peek(REG_PA_DAC), PA_DAC_ORDINARY);
    expect_hex("17 after 20: OCP back to 100 mA", rig.model.peek(REG_OCP), OCP_100MA);
}

/* The same pair through the FSK setter, and a MODE switch back to LoRa
 * reloads the boot defaults -- which is the third way out of 20. */
static void test_set_20_in_fsk_and_mode_switch_restores(void)
{
    Rig rig(true);
    RadioRfDefaults def = lora_defaults();
    expect_int("boot succeeds", rig.drv.begin(&def), RADIOLIB_ERR_NONE);

    expect_int("switch to FSK succeeds",
               rig.drv.switchMode(RADIO_MODE_FSK, &def), RADIOLIB_ERR_NONE);
    expect_hex("FSK entry: OCP re-pinned to 100 mA (beginFSK set 60)",
               rig.model.peek(REG_OCP), OCP_100MA);

    expect_int("FSK SET POWER=20 accepted with permission",
               rig.drv.applyFskParam("t", "POWER", "20"), RADIOLIB_ERR_NONE);
    nl();
    expect_hex("FSK 20: PA_DAC 0x87", rig.model.peek(REG_PA_DAC), PA_DAC_BOOST);
    expect_hex("FSK 20: OCP 140 mA", rig.model.peek(REG_OCP), OCP_140MA);

    expect_int("switch back to LoRa succeeds",
               rig.drv.switchMode(RADIO_MODE_LORA, &def), RADIOLIB_ERR_NONE);
    expect_hex("MODE switch: PA_DAC back to 0x84", rig.model.peek(REG_PA_DAC), PA_DAC_ORDINARY);
    expect_hex("MODE switch: OCP back to 100 mA", rig.model.peek(REG_OCP), OCP_100MA);
}

/* Permission OFF: the driver's own gate (defence in depth behind the
 * prevalidator) leaves the chip exactly where it was. */
static void test_without_permission_20_touches_no_register(void)
{
    Rig rig(false);
    RadioRfDefaults def = lora_defaults();
    expect_int("boot succeeds", rig.drv.begin(&def), RADIOLIB_ERR_NONE);

    rig.drv.applyLoraParam("t", "POWER", "20");
    nl();
    expect_hex("no permission, LoRa: PA_DAC untouched", rig.model.peek(REG_PA_DAC), PA_DAC_ORDINARY);
    expect_hex("no permission, LoRa: OCP untouched", rig.model.peek(REG_OCP), OCP_100MA);

    expect_int("switch to FSK succeeds",
               rig.drv.switchMode(RADIO_MODE_FSK, &def), RADIOLIB_ERR_NONE);
    rig.drv.applyFskParam("t", "POWER", "20");
    nl();
    expect_hex("no permission, FSK: PA_DAC untouched", rig.model.peek(REG_PA_DAC), PA_DAC_ORDINARY);
    expect_hex("no permission, FSK: OCP untouched", rig.model.peek(REG_OCP), OCP_100MA);
}

/* 18 and 19 never reach the chip, permission or not: RadioLib refuses them
 * before any register write, and the driver reports that. */
static void test_18_and_19_never_reach_the_chip(void)
{
    Rig rig(true);
    RadioRfDefaults def = lora_defaults();
    expect_int("boot succeeds", rig.drv.begin(&def), RADIOLIB_ERR_NONE);

    rig.drv.applyLoraParam("t", "POWER", "18");
    rig.drv.applyLoraParam("t", "POWER", "19");
    nl();
    expect_hex("18/19: PA_DAC untouched", rig.model.peek(REG_PA_DAC), PA_DAC_ORDINARY);
    expect_hex("18/19: OCP untouched", rig.model.peek(REG_OCP), OCP_100MA);
}

/* A rejected write is a returned error, never a swallowed one, and there is
 * no invented rollback: the dispatcher fails the radio closed on it. */
static void test_setter_failure_is_reported(void)
{
    {
        Rig rig(true);
        RadioRfDefaults def = lora_defaults();
        expect_int("boot succeeds", rig.drv.begin(&def), RADIOLIB_ERR_NONE);

        rig.model.reject_writes_to = REG_PA_DAC;
        int16_t st = rig.drv.applyLoraParam("t", "POWER", "20");
        nl();
        expect_int("PA_DAC write rejected -> error returned", st != RADIOLIB_ERR_NONE, 1);
        expect_hex("PA_DAC write rejected -> OCP never raised", rig.model.peek(REG_OCP), OCP_100MA);
    }
    {
        Rig rig(true);
        RadioRfDefaults def = lora_defaults();
        expect_int("boot succeeds", rig.drv.begin(&def), RADIOLIB_ERR_NONE);

        rig.model.reject_writes_to = REG_OCP;
        int16_t st = rig.drv.applyLoraParam("t", "POWER", "20");
        nl();
        expect_int("OCP write rejected -> error returned", st != RADIOLIB_ERR_NONE, 1);
    }
}

int main(void)
{
    test_boot_restores_the_ordinary_pair();
    test_set_20_then_17_in_lora();
    test_set_20_in_fsk_and_mode_switch_restores();
    test_without_permission_20_touches_no_register();
    test_18_and_19_never_reach_the_chip();
    test_setter_failure_is_reported();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
