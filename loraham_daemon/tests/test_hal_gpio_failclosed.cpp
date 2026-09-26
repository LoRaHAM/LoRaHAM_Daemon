/*
 * HAL GPIO failure semantics (hardware audit HW-2).
 *
 * RadioLib's PiHal logs a failed lgpio call and carries on. `digitalRead`
 * returns lgGpioRead()'s int through a uint32_t, so a negative error code
 * becomes a large POSITIVE value, which every RadioLib caller reads as a logic
 * HIGH. Three consequences, all reachable from the daemon's production paths:
 *
 *   1. SX127x::transmit() waits `while(!digitalRead(irq))`. A read error makes
 *      the condition false immediately, so the wait loop never runs once and
 *      control falls through to finishTransmit() -- which returns success AND
 *      puts the radio in standby. The daemon can cut a transmission short on
 *      air and then log the frame as sent.
 *   2. SX127x::scanChannel() has the same shape and returns CHANNEL_FREE, so a
 *      broken GPIO reports a clear channel to listen-before-talk.
 *   3. PiHal::init() stores the NEGATIVE handle when lgGpiochipOpen fails, so
 *      every later read returns LG_BAD_HANDLE -- the IRQ line reads high for
 *      the life of the process -- spiBegin() is skipped, and the guard
 *      `if(_gpioHandle != -1) return;` makes a retry believe it succeeded.
 *
 * These tests pin the CURRENT behaviour first, so the repair has something that
 * fails before it and passes after. They use the fake <lgpio.h> from
 * tests/fakes and provide every body themselves, so "the hardware" is whatever
 * the test says it is.
 */

#include "../locking_pihal.h"
#include "../sx127x_driver.h"

#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>

/* ---- tiny assert helpers, matching the convention in tests/ -------------- */

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

/* ---- injectable lgpio ---------------------------------------------------- */

namespace {

struct FakeGpio {
    int  chip_open_result = 7;      /* >= 0 is a handle; < 0 is a failure */
    int  read_result      = 0;      /* 0/1 normally, negative to inject */
    int  claim_input_result = 0;
    int  claim_alert_result = 0;
    int  write_result       = 0;
    int  free_result        = 0;
    int  set_alerts_result  = 0;
    int  reads              = 0;
    int  chip_opens         = 0;
    int  spi_opens          = 0;
    bool spi_open_called    = false;
};

FakeGpio g;

void reset_fake(void)
{
    g = FakeGpio();
}

}  /* namespace */

extern "C" {

int lgGpiochipOpen(int)                       { g.chip_opens++; return g.chip_open_result; }
int lgGpiochipClose(int)                      { return 0; }
int lgGpioClaimInput(int, int, int)           { return g.claim_input_result; }
int lgGpioClaimOutput(int, int, int, int)     { return 0; }
int lgGpioClaimAlert(int, int, int, int, int) { return g.claim_alert_result; }
int lgGpioFree(int, int)                      { return g.free_result; }
int lgGpioRead(int, int)                      { g.reads++; return g.read_result; }
int lgGpioWrite(int, int, int)                { return g.write_result; }
int lgGpioSetAlertsFunc(int, int, lgGpioAlertsFunc_t, void *) { return g.set_alerts_result; }

int lgTxPwm(int, int, float, float, int, int) { return 0; }

int lgSpiOpen(int, int, int, int) { g.spi_opens++; g.spi_open_called = true; return 11; }
int lgSpiClose(int)               { return 0; }
int lgSpiXfer(int, const char *, char *rx, int count)
{
    if (rx) memset(rx, 0, (size_t)count);
    return count;
}

const char *lguErrorText(int)  { return "fake lgpio error"; }
void        lguSleep(double)   { }
uint64_t    lguTimestamp(void) { return 0; }

}  /* extern "C" */

/* LockingPiHal takes an injectable flock(); these tests are about GPIO, so the
 * SPI lock always succeeds. */
static int fake_flock(int, int) { return 0; }

/* ---- tests --------------------------------------------------------------- */

/*
 * A GPIO read error must never be reported as a pin level. Today it is: the
 * negative code is returned through uint32_t, so the caller sees a huge
 * positive number and treats it as HIGH. This is the root of HW-2.
 */
static void test_read_error_is_not_a_level(void)
{
    reset_fake();

    /* init() FIRST, so the handle is genuinely open and this exercises the
     * read-error path rather than the no-handle path -- both return 0, so
     * without this the test would pass for the wrong reason. */
    LockingPiHal hal(0, 2000000, 0, 0, fake_flock);
    hal.init();
    g.read_result = LG_BAD_HANDLE;   /* -5 */
    uint32_t level = hal.digitalRead(25);
    char detail[160];
    snprintf(detail, sizeof(detail),
             "lgGpioRead() returned %d and digitalRead() surfaced it as %u",
             LG_BAD_HANDLE, (unsigned)level);

    expect("the read actually reached lgpio", g.reads > 0,
           "no lgGpioRead() call was made, so this test proves nothing");
    expect("read error is not reported as a pin level",
           level == 0 || level == 1, detail);
}

/*
 * The specific consequence: a negative code read as unsigned is TRUTHY, which
 * is what makes RadioLib's `while(!digitalRead(irq))` loops exit immediately
 * and report success. Stated as its own assertion so the failure message names
 * the mechanism rather than just a number.
 */
static void test_read_error_is_not_truthy(void)
{
    reset_fake();

    LockingPiHal hal(0, 2000000, 0, 0, fake_flock);
    hal.init();
    g.read_result = LG_GPIO_NOT_ALLOCATED;   /* -80 */
    uint32_t level = hal.digitalRead(25);
    char detail[200];
    snprintf(detail, sizeof(detail),
             "got %u, which is truthy; RadioLib's TX and CAD waits are "
             "`while(!digitalRead(irq))`, so this exits the wait at once and "
             "reports success", (unsigned)level);

    expect("read error is not truthy", !level, detail);
}

/*
 * A failed gpiochip open must not leave the HAL looking initialised. Today the
 * negative result is STORED as the handle, so every later read returns
 * LG_BAD_HANDLE, and because the retry guard tests `!= -1` a second init()
 * returns immediately believing it succeeded.
 */
static void test_failed_chip_open_does_not_look_initialised(void)
{
    reset_fake();
    g.chip_open_result = LG_BAD_HANDLE;

    LockingPiHal hal(0, 2000000, 0, 0, fake_flock);
    hal.init();

    expect("failed gpiochip open does not proceed to SPI",
           !g.spi_open_called, "spiBegin() ran after the GPIO open failed");

    /* A retry must actually retry, not short-circuit on a poisoned handle. */
    int opens_before = g.chip_opens;
    hal.init();
    expect("a retry after a failed open really re-attempts",
           g.chip_opens > opens_before,
           "the `!= -1` guard treats a stored negative handle as "
           "'already initialised', so init() never retries");
}

/*
 * Positive lifecycle case, and the one a fail-closed wrapper is most likely to
 * break: the daemon's own TX path clears the packet-received callback, runs a
 * blocking transmit that READS that pin, and reinstalls the callback after
 * (daemon_tx.cpp:117 and :218). A read on a pin whose alert was detached is
 * ORDINARY, not a violation.
 */
static void test_read_after_alert_detach_is_ordinary(void)
{
    reset_fake();

    LockingPiHal hal(0, 2000000, 0, 0, fake_flock);
    hal.init();

    hal.attachInterrupt(25, NULL, LG_RISING_EDGE);
    hal.detachInterrupt(25);

    g.read_result = 1;
    uint32_t level = hal.digitalRead(25);
    expect("a read after the alert was detached still works",
           level == 1,
           "every LoRa TX clears the callback, reads DIO0 in the blocking "
           "wait, then reinstalls it -- this must not be a violation");

    hal.attachInterrupt(25, NULL, LG_RISING_EDGE);
    expect("the alert can be re-attached after a detach",
           g.claim_alert_result == 0, "re-attach rejected");
}

/*
 * The latch is the only channel a GPIO failure has, and the daemon's exit code
 * hangs off it: lora_init() folds `!gpio_startup_ok()` into g_boot_lock_failed,
 * and daemon_io_runtime.cpp turns that into exit 4 instead of 1. Without that
 * signal a mis-wired or unpermitted box restarts every two seconds forever.
 */
static void test_startup_latch_reports_the_failure(void)
{
    reset_fake();

    LockingPiHal hal(0, 2000000, 0, 0, fake_flock);
    hal.init();
    expect("a clean startup leaves the latch clear", hal.gpio_startup_ok(),
           "the latch was already set before anything failed");

    g.read_result = LG_BAD_HANDLE;
    (void)hal.digitalRead(25);
    expect("a startup GPIO failure sets the latch", !hal.gpio_startup_ok(),
           "the failure is invisible to lora_init(), so the daemon would "
           "exit 1 and systemd would restart-spin on it");
}

/*
 * The other side of the same switch. Once the radio is up, a GPIO failure is
 * no longer a configuration problem to report -- it is loss of radio I/O
 * integrity, treated exactly as a failed SPI transfer already is: exit 5, which
 * systemd MAY restart (unlike 3 and 4). Proven in a child process, because the
 * contract is that the call does not return.
 */
static void test_operational_gpio_failure_exits_five(void)
{
    reset_fake();

    fflush(NULL);
    pid_t pid = fork();
    if (pid == 0) {
        LockingPiHal hal(0, 2000000, 0, 0, fake_flock);
        hal.init();
        hal.gpio_mark_operational();
        g.read_result = LG_BAD_READ;
        (void)hal.digitalRead(25);
        /* Reached only if the failure was swallowed. */
        _exit(0);
    }

    int status = 0;
    bool waited = (pid > 0) && (waitpid(pid, &status, 0) == pid);
    bool exited_five = waited && WIFEXITED(status) &&
                       WEXITSTATUS(status) == LORAHAM_EXIT_RUNTIME_RADIO_IO_ERROR;
    char detail[160];
    snprintf(detail, sizeof(detail),
             "expected exit %d, got %s %d", LORAHAM_EXIT_RUNTIME_RADIO_IO_ERROR,
             waited && WIFEXITED(status) ? "exit" : "signal/none",
             waited ? (WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status))
                    : -1);
    expect("an operational GPIO failure is restartable-fatal, not a level",
           exited_five, detail);
}

/*
 * The HAL's contract is that an unexpected negative lgpio result means
 * radio-I/O integrity is gone. detachInterrupt() was the one place that broke
 * it: it called lgGpioFree() and lgGpioSetAlertsFunc() and looked at neither
 * result. A detach happens before every LoRa transmission, so a failure there
 * would have been silent on the busiest path in the daemon.
 */
static void test_a_failed_detach_is_not_swallowed(void)
{
    reset_fake();

    LockingPiHal hal(0, 2000000, 0, 0, fake_flock);
    hal.init();
    hal.attachInterrupt(25, NULL, LG_RISING_EDGE);

    g.free_result = LG_GPIO_NOT_ALLOCATED;
    hal.detachInterrupt(25);

    expect("a failed lgGpioFree during detach is reported",
           !hal.gpio_startup_ok(),
           "the detach swallowed the error, so the contract that a negative "
           "lgpio result is never ignored does not hold on the TX path");
}

static void test_a_failed_alert_clear_is_not_swallowed(void)
{
    reset_fake();

    LockingPiHal hal(0, 2000000, 0, 0, fake_flock);
    hal.init();
    hal.attachInterrupt(25, NULL, LG_RISING_EDGE);

    g.set_alerts_result = LG_BAD_HANDLE;
    hal.detachInterrupt(25);

    expect("a failed lgGpioSetAlertsFunc during detach is reported",
           !hal.gpio_startup_ok(), "the detach swallowed the error");
}

/*
 * A failed gpiochip open must reach the daemon's NON-restartable exit.
 *
 * Found by an upstream diff review, and the reason the earlier tests missed it
 * is worth stating: they stopped at hal.init(). RadioLib does not. init()
 * latches the failure and returns void, then Module::init() and the SX127x
 * chip detection run regardless, and the detection's register read reaches the
 * SPI guard -- which used to kill the process with the RUNTIME code 5 before
 * lora_init() could read the latch and choose the startup code 4. A
 * permanently mis-wired or unpermitted box would then restart every two
 * seconds, which is exactly what code 4 exists to prevent.
 *
 * So this runs the REAL Sx127xDriver::begin() against the REAL pinned RadioLib
 * with the GPIO open failing, in a child process, and asserts the child is
 * still alive to report a latched failure rather than having been killed.
 */
static void test_gpio_open_failure_survives_real_radio_init(void)
{
    reset_fake();

    fflush(NULL);
    pid_t pid = fork();
    if (pid == 0) {
        g.chip_open_result = LG_NOT_PERMITTED;

        LockingPiHal hal(0, 2000000, 0, 0, fake_flock);
        Module mod(&hal, 8, 25, 5, 24);
        Sx127xDriver driver(&mod, false);

        RadioRfDefaults defaults{};
        defaults.freq_mhz = 433.775f;
        defaults.spreading_factor = 12;
        defaults.bandwidth_khz = 125.0f;
        defaults.sync_word = 0x12;
        defaults.preamble_len = 8;
        defaults.coding_rate = 5;
        defaults.crc_on = true;
        defaults.ldro = -1;
        defaults.power_dbm = 17;

        (void)driver.begin(&defaults);

        /* Reached only if begin() returned instead of the process being
         * killed. 0 means the latch was lost, which would be its own defect. */
        _exit(hal.gpio_startup_ok() ? 0 : LORAHAM_EXIT_LOCK_ERROR);
    }

    int status = 0;
    const bool waited = (pid > 0) && (waitpid(pid, &status, 0) == pid);
    const int code = (waited && WIFEXITED(status)) ? WEXITSTATUS(status) : -1;

    char detail[200];
    snprintf(detail, sizeof(detail),
             "child exited %d; %d is the runtime radio-I/O code, which would "
             "make systemd restart-spin on a permanently broken box",
             code, LORAHAM_EXIT_RUNTIME_RADIO_IO_ERROR);

    expect("a GPIO open failure does not become a runtime fatal",
           code != LORAHAM_EXIT_RUNTIME_RADIO_IO_ERROR, detail);
    expect("real radio init leaves the startup latch set, so lora_init() can "
           "choose the non-restartable exit",
           code == LORAHAM_EXIT_LOCK_ERROR, detail);
}

int main(void)
{
    test_read_error_is_not_a_level();
    test_read_error_is_not_truthy();
    test_failed_chip_open_does_not_look_initialised();
    test_read_after_alert_detach_is_ordinary();
    test_startup_latch_reports_the_failure();
    test_operational_gpio_failure_exits_five();
    test_a_failed_detach_is_not_swallowed();
    test_gpio_open_failure_survives_real_radio_init();
    test_a_failed_alert_clear_is_not_swallowed();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
