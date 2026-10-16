/*
 * Kernel chip selects through the real HAL and RadioLib's Module, with lgpio
 * faked (tests/fakes/lgpio.h): the radio wiring the daemon builds
 * (radio_wiring.h) must open the spidev of its CE line and never claim that
 * line as a GPIO. The soft row is today's behaviour (1.2.0 built exactly that:
 * LockingPiHal(0) and Module(hal, cs, ...)): spidev0.0 and a GPIO claim.
 */

#include "../radio_wiring.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

namespace {
struct Seen {
    int spi_dev = -1;
    int spi_chan = -1;
    int claimed[16];
    int n_claimed = 0;
} g;

bool claimed(int gpio)
{
    for (int i = 0; i < g.n_claimed; i++)
        if (g.claimed[i] == gpio)
            return true;
    return false;
}
}  /* namespace */

extern "C" {
int lgGpiochipOpen(int)                       { return 7; }
int lgGpiochipClose(int)                      { return 0; }
int lgGpioClaimInput(int, int, int gpio)
{
    if (g.n_claimed < 16) g.claimed[g.n_claimed++] = gpio;
    return 0;
}
int lgGpioClaimOutput(int, int, int gpio, int)
{
    if (g.n_claimed < 16) g.claimed[g.n_claimed++] = gpio;
    return 0;
}
int lgGpioClaimAlert(int, int, int, int, int) { return 0; }
int lgGpioFree(int, int)                      { return 0; }
int lgGpioRead(int, int)                      { return 0; }
int lgGpioWrite(int, int, int)                { return 0; }
int lgGpioSetAlertsFunc(int, int, lgGpioAlertsFunc_t, void *) { return 0; }
int lgTxPwm(int, int, float, float, int, int) { return 0; }
int lgSpiOpen(int dev, int chan, int, int)    { g.spi_dev = dev; g.spi_chan = chan; return 11; }
int lgSpiClose(int)                           { return 0; }
int lgSpiXfer(int, const char *, char *rx, int count)
{
    if (rx) memset(rx, 0, (size_t)count);
    return count;
}
const char *lguErrorText(int)  { return "fake lgpio error"; }
void        lguSleep(double)   { }
uint64_t    lguTimestamp(void) { return 0; }
}  /* extern "C" */

static int fake_flock(int, int) { return 0; }

/* Build the wiring for the loraham 868 radio (CS = BCM 7) and do one register
 * read through Module, as begin() would. */
static void wire_868(bool kernel_cs)
{
    g = Seen();
    daemon_set_hardware_preset("loraham");
    daemon_hardware_profile_resolve(868);
    daemon_hardware_profile_apply_spi(&daemon_hw_profile, kernel_cs);
    LockingPiHal *hal = daemon_radio_hal(daemon_hw_profile, fake_flock);
    Module *mod = daemon_radio_module(hal, daemon_hw_profile);
    hal->init();
    mod->init();
    (void)mod->SPIreadRegister(0x42);
    delete mod;
    delete hal;
}

static void test_kernel_cs7_opens_spidev01_and_claims_no_cs_line(void)
{
    wire_868(true);
    char detail[96];
    snprintf(detail, sizeof(detail), "lgSpiOpen(%d, %d)", g.spi_dev, g.spi_chan);
    expect("kernel: spidev0.1 opened", g.spi_dev == 0 && g.spi_chan == 1, detail);
    expect("kernel: GPIO 7 never claimed", !claimed(7), "Module claimed the CE1 line");
}

static void test_soft_cs7_opens_spidev00_and_claims_the_cs_line(void)
{
    wire_868(false);
    char detail[96];
    snprintf(detail, sizeof(detail), "lgSpiOpen(%d, %d)", g.spi_dev, g.spi_chan);
    expect("soft: spidev0.0 opened", g.spi_dev == 0 && g.spi_chan == 0, detail);
    expect("soft: GPIO 7 claimed as the chip select", claimed(7), "no claim of 7");
}

int main(void)
{
    /* The HAL opens the process-shared SPI lock in a trusted directory; give it
     * a private one, as test_locking_pihal does. */
    char dir[64];
    snprintf(dir, sizeof(dir), "/tmp/loraham-test-kcs-%d", (int)getpid());
    mkdir(dir, 0700);
    setenv("LORAHAM_RUNTIME_DIR", dir, 1);

    test_kernel_cs7_opens_spidev01_and_claims_no_cs_line();
    test_soft_cs7_opens_spidev00_and_claims_the_cs_line();

    char lock[96];
    snprintf(lock, sizeof(lock), "%s/spi0.lock", dir);
    unlink(lock);
    rmdir(dir);

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
