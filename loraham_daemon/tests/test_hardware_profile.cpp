#include "../hardware_profile.h"

#include <stdio.h>
#include <string.h>

/* --- Hardware profile preset tests --------------------------------------- */

static int g_ok = 0;
static int g_fail = 0;

static void expect_int(const char *name, long actual, long expected)
{
    if (actual == expected) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: expected %ld, got %ld\n", name, expected, actual);
    }
}

static void expect_str(const char *name, const char *actual, const char *expected)
{
    if (actual && strcmp(actual, expected) == 0) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: expected %s, got %s\n", name, expected,
               actual ? actual : "(null)");
    }
}

static int claimed_contains(const DaemonHardwareProfile *p, int pin)
{
    for (int i = 0; i < p->claimed_count; i++) {
        if (p->claimed[i] == pin)
            return 1;
    }
    return 0;
}

/* loraham (canonical) must resolve per band exactly to the LoRaHAM_Pi wiring. */
static void test_loraham_433(void)
{
    expect_int("set loraham", daemon_set_hardware_preset("loraham"), 1);
    expect_int("resolve loraham 433", daemon_hardware_profile_resolve(433), 1);
    expect_str("loraham name", daemon_hw_profile.name, "loraham");
    expect_int("legacy family sx127x", daemon_hw_profile.family,
               DAEMON_CHIP_FAMILY_SX127X);
    expect_int("legacy 433 cs", daemon_hw_profile.cs, 8);
    expect_int("legacy 433 irq/dio0", daemon_hw_profile.irq, 25);
    expect_int("legacy 433 rst", daemon_hw_profile.rst, 5);
    expect_int("legacy 433 gpio/dio1", daemon_hw_profile.gpio, 24);
    expect_int("legacy 433 led", daemon_hw_profile.led_pin, 13);
    expect_int("legacy cad scan", daemon_hw_profile.cad_scan_available, 1);
    expect_int("legacy fsk stream", daemon_hw_profile.fsk_stream_available, 1);
    expect_int("legacy reset wired", daemon_hw_profile.reset_wired, 1);
}

static void test_loraham_868(void)
{
    expect_int("resolve legacy 868", daemon_hardware_profile_resolve(868), 1);
    expect_int("legacy 868 cs", daemon_hw_profile.cs, 7);
    expect_int("legacy 868 irq/dio0", daemon_hw_profile.irq, 16);
    expect_int("legacy 868 rst", daemon_hw_profile.rst, 6);
    expect_int("legacy 868 gpio/dio1", daemon_hw_profile.gpio, 12);
    expect_int("legacy 868 led", daemon_hw_profile.led_pin, 19);
}


/* Uputronics: DIO1 and RESET not routed; LED slot-based; band-agnostic. */
static void test_uputronics_ce0(void)
{
    expect_int("set ce0", daemon_set_hardware_preset("uputronics-ce0"), 1);
    expect_int("resolve ce0 433", daemon_hardware_profile_resolve(433), 1);
    expect_str("ce0 name", daemon_hw_profile.name, "uputronics-ce0");
    expect_int("ce0 cs", daemon_hw_profile.cs, 8);
    expect_int("ce0 irq/dio0", daemon_hw_profile.irq, 25);
    expect_int("ce0 rst NC", daemon_hw_profile.rst < 0, 1);
    expect_int("ce0 dio1 NC", daemon_hw_profile.gpio < 0, 1);
    expect_int("ce0 aux dio5 doc", daemon_hw_profile.aux, 24);
    expect_int("ce0 led", daemon_hw_profile.led_pin, 6);
    /* Register-polled CAD needs no DIO1, so this board has trustworthy
     * active CAD despite gpio=NC asserted just above. */
    expect_int("ce0 cad scan despite no DIO1",
               daemon_hw_profile.cad_scan_available, 1);
    expect_int("ce0 no fsk stream", daemon_hw_profile.fsk_stream_available, 0);
    expect_int("ce0 reset not wired", daemon_hw_profile.reset_wired, 0);
    expect_int("ce0 claims cs", claimed_contains(&daemon_hw_profile, 8), 1);
    expect_int("ce0 claims dio0", claimed_contains(&daemon_hw_profile, 25), 1);
    expect_int("ce0 claims led", claimed_contains(&daemon_hw_profile, 6), 1);
    expect_int("ce0 claims no rst", claimed_contains(&daemon_hw_profile, 5), 0);

    /* Same wiring for the 868 binding (band comes from --radio). */
    expect_int("resolve ce0 868", daemon_hardware_profile_resolve(868), 1);
    expect_int("ce0 868 cs unchanged", daemon_hw_profile.cs, 8);
}

static void test_uputronics_ce1(void)
{
    expect_int("set ce1", daemon_set_hardware_preset("uputronics-ce1"), 1);
    expect_int("resolve ce1 433", daemon_hardware_profile_resolve(433), 1);
    expect_int("ce1 cs", daemon_hw_profile.cs, 7);
    expect_int("ce1 irq/dio0", daemon_hw_profile.irq, 16);
    expect_int("ce1 rst NC", daemon_hw_profile.rst < 0, 1);
    expect_int("ce1 dio1 NC", daemon_hw_profile.gpio < 0, 1);
    expect_int("ce1 aux dio5 doc", daemon_hw_profile.aux, 12);
    expect_int("ce1 led", daemon_hw_profile.led_pin, 13);
    /* Register-polled CAD needs no DIO1, so this board has trustworthy
     * active CAD despite gpio=NC asserted just above. */
    expect_int("ce1 cad scan despite no DIO1",
               daemon_hw_profile.cad_scan_available, 1);
}

static void test_waveshare(void)
{
    expect_int("set waveshare", daemon_set_hardware_preset("waveshare-sx1262"), 1);
    expect_int("resolve waveshare 868", daemon_hardware_profile_resolve(868), 1);
    expect_int("ws family sx1262", daemon_hw_profile.family,
               DAEMON_CHIP_FAMILY_SX1262);
    expect_int("ws cs", daemon_hw_profile.cs, 21);
    expect_int("ws irq/dio1", daemon_hw_profile.irq, 16);
    expect_int("ws rst", daemon_hw_profile.rst, 18);
    expect_int("ws gpio/busy", daemon_hw_profile.gpio, 20);
    expect_int("ws txen", daemon_hw_profile.txen, 6);
    expect_int("ws rxen NC (DIO2 internal)", daemon_hw_profile.rxen < 0, 1);
    expect_int("ws tcxo set", daemon_hw_profile.tcxo_voltage > 0.0f, 1);
    expect_int("ws led NC", daemon_hw_profile.led_pin < 0, 1);
    expect_int("ws cad scan", daemon_hw_profile.cad_scan_available, 1);
    expect_int("ws led not claimed", claimed_contains(&daemon_hw_profile, 13), 0);
    expect_int("ws claims txen 6", claimed_contains(&daemon_hw_profile, 6), 1);

    /* LF and HF variants are pin-identical: 433 binding resolves the same. */
    expect_int("resolve waveshare 433", daemon_hardware_profile_resolve(433), 1);
    expect_int("ws 433 cs unchanged", daemon_hw_profile.cs, 21);
}

static void test_unknown_preset(void)
{
    expect_int("set unknown records name",
               daemon_set_hardware_preset("does-not-exist"), 1);
    expect_int("resolve unknown fails", daemon_hardware_profile_resolve(433), 0);
    expect_int("set null fails", daemon_set_hardware_preset(NULL), 0);
    expect_int("set empty fails", daemon_set_hardware_preset(""), 0);
}

static void test_family_names(void)
{
    expect_str("family name sx127x",
               daemon_chip_family_name(DAEMON_CHIP_FAMILY_SX127X), "SX127x");
    expect_str("family name sx1262",
               daemon_chip_family_name(DAEMON_CHIP_FAMILY_SX1262), "SX1262");
}

/* The chip-select mode, the same four rows in every bus user: no kernel
 * chip selects / CS 8 / CS 7 / any other CS. */
static void test_spi_soft_cs_keeps_the_profile(void)
{
    daemon_set_hardware_preset("loraham");
    daemon_hardware_profile_resolve(868);
    expect_int("soft: applied", daemon_hardware_profile_apply_spi(&daemon_hw_profile, false), 1);
    expect_int("soft: cs stays GPIO 7", daemon_hw_profile.cs, 7);
    expect_int("soft: spidev0.0", daemon_hw_profile.spi_channel, 0);
    expect_int("soft: not kernel", daemon_hw_profile.kernel_cs, 0);
    expect_int("soft: GPIO 7 claimed", claimed_contains(&daemon_hw_profile, 7), 1);
}

static void test_spi_kernel_cs8_uses_spidev00_without_the_gpio(void)
{
    daemon_set_hardware_preset("uputronics-ce0");
    daemon_hardware_profile_resolve(433);
    expect_int("ce0: applied", daemon_hardware_profile_apply_spi(&daemon_hw_profile, true), 1);
    expect_int("ce0: cs still names BCM 8", daemon_hw_profile.cs, 8);
    expect_int("ce0: spidev0.0", daemon_hw_profile.spi_channel, 0);
    expect_int("ce0: kernel", daemon_hw_profile.kernel_cs, 1);
    expect_int("ce0: GPIO 8 not claimed", claimed_contains(&daemon_hw_profile, 8), 0);
    expect_int("ce0: IRQ 25 still claimed", claimed_contains(&daemon_hw_profile, 25), 1);
}

static void test_spi_kernel_cs7_uses_spidev01_without_the_gpio(void)
{
    daemon_set_hardware_preset("loraham");
    daemon_hardware_profile_resolve(868);
    expect_int("ce1: applied", daemon_hardware_profile_apply_spi(&daemon_hw_profile, true), 1);
    expect_int("ce1: cs still names BCM 7", daemon_hw_profile.cs, 7);
    expect_int("ce1: spidev0.1", daemon_hw_profile.spi_channel, 1);
    expect_int("ce1: GPIO 7 not claimed", claimed_contains(&daemon_hw_profile, 7), 0);
    expect_int("ce1: LED 19 still claimed", claimed_contains(&daemon_hw_profile, 19), 1);
}

static void test_spi_kernel_cs_refuses_a_cs_off_ce0_ce1(void)
{
    daemon_set_hardware_preset("waveshare-sx1262");
    daemon_hardware_profile_resolve(433);
    expect_int("cs21: refused", daemon_hardware_profile_apply_spi(&daemon_hw_profile, true), 0);
}

int main(void)
{
    test_loraham_433();
    test_loraham_868();
    test_uputronics_ce0();
    test_uputronics_ce1();
    test_waveshare();
    test_unknown_preset();
    test_family_names();
    test_spi_soft_cs_keeps_the_profile();
    test_spi_kernel_cs8_uses_spidev00_without_the_gpio();
    test_spi_kernel_cs7_uses_spidev01_without_the_gpio();
    test_spi_kernel_cs_refuses_a_cs_off_ce0_ce1();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
