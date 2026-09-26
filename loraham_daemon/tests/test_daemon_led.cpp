#include "../daemon_led.h"
#include "../daemon_radio_runtime.h"
#include "../daemon_band.h"

#include <stdio.h>
#include <string.h>

/* --- LED-Tests ------------------------------------------------------------ */

static int g_ok = 0;
static int g_fail = 0;

static int g_open_result = 7;
static int g_claim_433_result = 0;
static int g_claim_868_result = 0;

static int g_open_count = 0;
static int g_close_count = 0;
static int g_claim_count = 0;
static int g_write_count = 0;
static int g_free_count = 0;

static unsigned g_claim_pins[4];
static int g_claim_levels[4];
static unsigned g_write_pins[8];
static int g_write_levels[8];
static unsigned g_free_pins[4];

// Last GPIO write, tracked independently of the bounded arrays above so the
// sync_led tests can assert the final LED state after many writes.
static int g_last_write_pin = -1;
static int g_last_write_level = -1;

extern "C" int lgGpiochipOpen(int gpio_dev)
{
    (void)gpio_dev;
    g_open_count++;
    return g_open_result;
}

extern "C" int lgGpiochipClose(int handle)
{
    (void)handle;
    g_close_count++;
    return 0;
}

extern "C" int lgGpioClaimOutput(int handle, int flags,
                                 int gpio, int level)
{
    (void)handle;
    (void)flags;

    if (g_claim_count < (int)(sizeof(g_claim_pins) / sizeof(g_claim_pins[0]))) {
        g_claim_pins[g_claim_count] = (unsigned)gpio;
        g_claim_levels[g_claim_count] = level;
    }
    g_claim_count++;

    if (gpio == DAEMON_LED_PIN_433)
        return g_claim_433_result;

    if (gpio == DAEMON_LED_PIN_868)
        return g_claim_868_result;

    return -99;
}

extern "C" int lgGpioWrite(int handle, int gpio, int level)
{
    (void)handle;

    if (g_write_count < (int)(sizeof(g_write_pins) / sizeof(g_write_pins[0]))) {
        g_write_pins[g_write_count] = (unsigned)gpio;
        g_write_levels[g_write_count] = level;
    }
    g_write_count++;
    g_last_write_pin = gpio;
    g_last_write_level = level;
    return 0;
}

extern "C" int lgGpioFree(int handle, int gpio)
{
    (void)handle;

    if (g_free_count < (int)(sizeof(g_free_pins) / sizeof(g_free_pins[0])))
        g_free_pins[g_free_count] = (unsigned)gpio;
    g_free_count++;
    return 0;
}

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

static void reset_fake(void)
{
    /* Resolve the band BEFORE shutdown: the pin lookup falls back to the
     * band descriptor, which fails closed when unresolved. Default band for
     * the legacy lifecycle/sync tests is 433. */
    daemon_band_resolve(RADIO_BAND_433);
    daemon_led_configure(DAEMON_LED_PIN_433);

    daemon_led_shutdown();

    g_open_result = 7;
    g_claim_433_result = 0;
    g_claim_868_result = 0;

    g_open_count = 0;
    g_close_count = 0;
    g_claim_count = 0;
    g_write_count = 0;
    g_free_count = 0;

    g_last_write_pin = -1;
    g_last_write_level = -1;

    memset(g_claim_pins, 0, sizeof(g_claim_pins));
    memset(g_claim_levels, 0, sizeof(g_claim_levels));
    memset(g_write_pins, 0, sizeof(g_write_pins));
    memset(g_write_levels, 0, sizeof(g_write_levels));
    memset(g_free_pins, 0, sizeof(g_free_pins));
}

static void test_successful_lifecycle(void)
{
    reset_fake();

    daemon_led_init();

    expect_int("LED ready after init", daemon_led_ready(), 1);
    expect_int("LED opens one chip", g_open_count, 1);
    expect_int("LED claims one pin", g_claim_count, 1);
    expect_int("LED claims GPIO13", (int)g_claim_pins[0], DAEMON_LED_PIN_433);
    expect_int("LED GPIO13 initial low", g_claim_levels[0], 0);

    daemon_led_set_pin(DAEMON_LED_PIN_433, 1);
    expect_int("LED write after init", g_write_count, 2);
    expect_int("LED write GPIO13", (int)g_write_pins[1], DAEMON_LED_PIN_433);
    expect_int("LED write on", g_write_levels[1], 1);

    daemon_led_shutdown();

    expect_int("LED not ready after shutdown", daemon_led_ready(), 0);
    expect_int("LED shutdown frees selected pin", g_free_count, 1);
    expect_int("LED shutdown frees GPIO13", (int)g_free_pins[0], DAEMON_LED_PIN_433);
    expect_int("LED shutdown closes chip", g_close_count, 1);
    expect_int("LED shutdown final write count", g_write_count, 3);
    expect_int("LED shutdown GPIO13 low", (int)g_write_pins[2], DAEMON_LED_PIN_433);
    expect_int("LED shutdown level low", g_write_levels[2], 0);
}

static void test_chip_open_failure(void)
{
    reset_fake();
    g_open_result = -1;

    daemon_led_init();

    expect_int("chip open failure leaves LED not ready", daemon_led_ready(), 0);
    expect_int("chip open failure tries once", g_open_count, 1);
    expect_int("chip open failure claims nothing", g_claim_count, 0);
    expect_int("chip open failure closes nothing", g_close_count, 0);
}

/* --- per-band ownership / selection-aware claim tests ------------------- */

static void test_radio_433_only_claims_only_433(void)
{
    reset_fake();
    daemon_band_resolve(RADIO_BAND_433);
    daemon_led_configure(DAEMON_LED_PIN_433);

    int rc = daemon_led_init();

    expect_int("433-only init succeeds", rc, 0);
    expect_int("433-only ready", daemon_led_ready(), 1);
    expect_int("433-only claims exactly one pin", g_claim_count, 1);
    expect_int("433-only claims GPIO13", (int)g_claim_pins[0], DAEMON_LED_PIN_433);

    daemon_led_shutdown();
    expect_int("433-only frees exactly one pin", g_free_count, 1);
    expect_int("433-only freed GPIO13", (int)g_free_pins[0], DAEMON_LED_PIN_433);
}

static void test_radio_868_only_claims_only_868(void)
{
    reset_fake();
    daemon_band_resolve(RADIO_BAND_868);
    daemon_led_configure(DAEMON_LED_PIN_868);

    int rc = daemon_led_init();

    expect_int("868-only init succeeds", rc, 0);
    expect_int("868-only ready", daemon_led_ready(), 1);
    expect_int("868-only claims exactly one pin", g_claim_count, 1);
    expect_int("868-only claims GPIO19", (int)g_claim_pins[0], DAEMON_LED_PIN_868);

    daemon_led_shutdown();
    expect_int("868-only frees exactly one pin", g_free_count, 1);
    expect_int("868-only freed GPIO19", (int)g_free_pins[0], DAEMON_LED_PIN_868);
}

// A selected band whose LED line is already owned (claim fails) must make
// init fatal and leave the daemon not-ready -- this is the duplicate-instance
// rejection at the unit level.
static void test_radio_433_only_busy_is_fatal(void)
{
    reset_fake();
    daemon_band_resolve(RADIO_BAND_433);
    daemon_led_configure(DAEMON_LED_PIN_433);
    g_claim_433_result = -5;            // GPIO13 already owned

    int rc = daemon_led_init();

    expect_int("433-only busy init fatal", rc, -1);
    expect_int("433-only busy not ready", daemon_led_ready(), 0);
    expect_int("433-only busy tried only 433", g_claim_count, 1);
    expect_int("433-only busy closes chip", g_close_count, 1);
}

static void test_radio_868_only_busy_is_fatal(void)
{
    reset_fake();
    daemon_band_resolve(RADIO_BAND_868);
    daemon_led_configure(DAEMON_LED_PIN_868);
    g_claim_868_result = -5;            // GPIO19 already owned

    int rc = daemon_led_init();

    expect_int("868-only busy init fatal", rc, -1);
    expect_int("868-only busy not ready", daemon_led_ready(), 0);
    expect_int("868-only busy tried only 868", g_claim_count, 1);
    expect_int("868-only busy claimed GPIO19", (int)g_claim_pins[0], DAEMON_LED_PIN_868);
    expect_int("868-only busy closes chip", g_close_count, 1);
}

// Profile without an LED line (pin < 0): no claim, init healthy, writes are
// no-ops — the LED feature is cleanly disabled instead of fatal.
static void test_led_disabled_by_profile(void)
{
    reset_fake();
    daemon_band_resolve(RADIO_BAND_433);
    daemon_led_configure(DAEMON_LED_PIN_433);
    daemon_led_configure(-1);

    int rc = daemon_led_init();

    expect_int("nc init succeeds", rc, 0);
    expect_int("nc ready (feature disabled)", daemon_led_ready(), 1);
    expect_int("nc claims nothing", g_claim_count, 0);

    daemon_led_set_pin(-1, 1);
    expect_int("nc write is no-op", g_write_count, 0);

    daemon_led_shutdown();
    expect_int("nc frees nothing", g_free_count, 0);

    /* Restore the legacy default pin for the remaining tests. */
    daemon_led_configure(DAEMON_LED_PIN_433);
}

/* --- sync_led derived-state tests --------------------------------------- */
// sync_led never touches the radio/driver, only the atomics — the plain
// RadioController (without driver) is enough.

static void fake_rx_callback(void)
{
}

static void init_led_ctrl(RadioController *ctrl)
{
    radio_controller_init(ctrl,
                          RADIO_BAND_433,
                          "TEST",
                          false,
                          fake_rx_callback,
                          DAEMON_LED_PIN_433);
}

// Calls sync_led and asserts a write happened to the band pin at `level`.
static void expect_sync_write(RadioController *ctrl,
                              const char *name,
                              int level)
{
    int before = g_write_count;

    daemon_radio_runtime_sync_led(ctrl);

    if (g_write_count != before + 1) {
        g_fail++;
        printf("[FAIL] %s: expected a write, count %d -> %d\n",
               name, before, g_write_count);
        return;
    }

    expect_int(name, g_last_write_level, level);
    expect_int("sync write targets band pin", g_last_write_pin, ctrl->led_pin);
}

// Calls sync_led and asserts the cache suppressed the redundant GPIO write.
static void expect_sync_no_write(RadioController *ctrl,
                                 const char *name)
{
    int before = g_write_count;

    daemon_radio_runtime_sync_led(ctrl);
    expect_int(name, g_write_count, before);
}

static void test_sync_led_derived_state(void)
{
    RadioController ctrl;

    reset_fake();
    daemon_led_init();
    init_led_ctrl(&ctrl);

    // OFF when both atomics are false.
    ctrl.tx_busy.store(false);
    ctrl.cad_broadcast_active.store(false);
    expect_sync_write(&ctrl, "sync off when idle", 0);

    // ON when tx_busy is true (cad still false).
    ctrl.tx_busy.store(true);
    expect_sync_write(&ctrl, "sync on when tx busy", 1);

    // Redundant call with unchanged state writes nothing (cache).
    expect_sync_no_write(&ctrl, "sync caches steady on");

    // Still ON when both are true.
    ctrl.cad_broadcast_active.store(true);
    expect_sync_no_write(&ctrl, "sync stays on tx+cad");

    // Back to idle -> OFF.
    ctrl.tx_busy.store(false);
    ctrl.cad_broadcast_active.store(false);
    expect_sync_write(&ctrl, "sync off when both clear", 0);

    // ON when only cad is busy (tx false).
    ctrl.cad_broadcast_active.store(true);
    expect_sync_write(&ctrl, "sync on when cad busy", 1);

    // OFF again when both return to false.
    ctrl.cad_broadcast_active.store(false);
    expect_sync_write(&ctrl, "sync off again", 0);
}

static void test_sync_led_no_rx_latch(void)
{
    RadioController ctrl;

    reset_fake();
    daemon_led_init();
    init_led_ctrl(&ctrl);

    // Spurious/empty-IRQ regression: RX observing a flag must never hold the
    // LED on. sync_led ignores `received`, so with both atomics false the LED
    // is OFF even while an RX flag is pending.
    ctrl.tx_busy.store(false);
    ctrl.cad_broadcast_active.store(false);
    ctrl.received.store(true);
    expect_sync_write(&ctrl, "pending rx flag does not latch led", 0);

    // RX-during-TX discard regression: while transmitting the LED is ON; once
    // TX clears (the discard path leaves the LED untouched) reconciliation
    // turns it OFF.
    ctrl.tx_busy.store(true);
    expect_sync_write(&ctrl, "led on during tx", 1);

    ctrl.received.store(false);   // discard path clears the RX flag, no LED write
    ctrl.tx_busy.store(false);
    expect_sync_write(&ctrl, "led off after tx ends", 0);
}

static void test_sync_led_cad_off_edge(void)
{
    RadioController ctrl;

    reset_fake();
    daemon_led_init();
    init_led_ctrl(&ctrl);

    // Channel busy -> ON.
    ctrl.cad_broadcast_active.store(true);
    expect_sync_write(&ctrl, "cad busy turns led on", 1);

    // Channel free while an RX flag is still pending. The old code gated the
    // OFF on !received and latched; sync_led derives purely from the atomics.
    ctrl.received.store(true);
    ctrl.cad_broadcast_active.store(false);
    expect_sync_write(&ctrl, "cad free turns led off despite rx flag", 0);
}

int main(void)
{
    test_successful_lifecycle();
    test_chip_open_failure();
    test_radio_433_only_claims_only_433();
    test_radio_868_only_claims_only_868();
    test_radio_433_only_busy_is_fatal();
    test_radio_868_only_busy_is_fatal();
    test_led_disabled_by_profile();
    test_sync_led_derived_state();
    test_sync_led_no_rx_latch();
    test_sync_led_cad_off_edge();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
