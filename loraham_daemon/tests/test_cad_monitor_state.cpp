#include "../daemon_cad_monitor.h"

#include <stdio.h>
#include <string.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

/* --- CAD monitor state machine tests ------------------------------------- */
/*
 * Covers the opt-in CAD=0/1 CONF monitor: edge emission, the lost-falling-edge
 * regression (RX-pending must not suppress CAD=0), and the free-confirmation
 * hysteresis (busy immediate at >= threshold; free after
 * DAEMON_CAD_FREE_CONFIRM_SAMPLES consecutive samples at least
 * DAEMON_CAD_FREE_HYSTERESIS_DB below it; dead band retains state).
 *
 * Default threshold is -90 dBm, so the free-confirm level is -93 dBm.
 */

static int g_ok = 0;
static int g_fail = 0;

// Fake driver: overrides the virtual RadioDriver delegates and counts calls
// exactly as the earlier template fake did (counter semantics unchanged).
struct FakeRadio : public RadioDriver {
    int scan_result;
    int scan_count;
    int callback_count;
    int start_receive_count;
    int clear_irq_count;
    float rssi;
    void (*last_callback)(void);

    FakeRadio() : RadioDriver(NULL),
                  scan_result(0), scan_count(0), callback_count(0),
                  start_receive_count(0), clear_irq_count(0),
                  rssi(-120.0f), last_callback(NULL) {}

    void setPacketReceivedAction(void (*cb)(void)) override
    {
        last_callback = cb;
        callback_count++;
    }

    int16_t startReceive() override
    {
        start_receive_count++;
        return 0;
    }

    int16_t clearIrq(uint32_t) override
    {
        clear_irq_count++;
        return 0;
    }

    int16_t scanChannel() override
    {
        scan_count++;
        return scan_result;
    }

    float getRSSI() override
    {
        return rssi;
    }

    float rssiProbe() override
    {
        return rssi;
    }

    int16_t begin(const RadioRfDefaults *) override { return 0; }
    int16_t switchMode(RadioMode_t,
                       const RadioRfDefaults *) override { return 0; }
    int16_t applyLoraParam(const char *, const std::string &,
                           const std::string &) override { return 0; }
    int16_t applyFskParam(const char *, const std::string &,
                          const std::string &) override { return 0; }
    float readLiveRssi(RadioMode_t, bool) override { return -200.0f; }
    /* The active CAD probe now stops reception before deciding whether a
     * packet is pending, so every fake needs both of these: the base class
     * forwards to phy_, which is NULL here. */
    int standby_count = 0;
    int16_t standby() override { standby_count++; return 0; }

    /* Mandatory since RadioDriver::rxDonePending() became pure virtual: a
     * default of false silently preserved the RX-erasure defect in any driver
     * that forgot it. These fakes never have a packet pending. */
    bool rxDonePending() override { return false; }

    const char *chipName() const override { return "FAKE"; }
    DaemonChipFamily chipFamily() const override
    {
        return DAEMON_CHIP_FAMILY_SX127X;
    }
};

static FakeRadio *fake(RadioController *ctrl)
{
    return static_cast<FakeRadio *>(ctrl->driver.get());
}

static void fake_rx_callback(void)
{
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

static void init_ctrl(RadioController *ctrl,
                      RadioHealth health,
                      RadioMode_t mode)
{
    radio_controller_init(ctrl,
                          RADIO_BAND_433,
                          "TEST",
                          false,
                          fake_rx_callback,
                          13);
    ctrl->driver.reset(new FakeRadio());
    ctrl->health = health;
    ctrl->mode = mode;
}

static int tick_edge(RadioController *ctrl, float rssi)
{
    fake(ctrl)->rssi = rssi;
    return daemon_cad_monitor_tick(ctrl).edge;
}

/* --- Pure helper: daemon_monitoring_cad_next_busy ------------------------ */

static void test_next_busy_asserts_immediately(void)
{
    int streak = 1;

    expect_int("next_busy free above threshold busy",
               daemon_monitoring_cad_next_busy(0, -80.0f, -90.0f, &streak), 1);
    expect_int("next_busy busy sample resets streak", streak, 0);

    streak = 0;
    expect_int("next_busy exactly at threshold busy",
               daemon_monitoring_cad_next_busy(0, -90.0f, -90.0f, &streak), 1);
}

static void test_next_busy_free_stays_free(void)
{
    int streak = 0;

    expect_int("next_busy free clearly-free stays free",
               daemon_monitoring_cad_next_busy(0, -95.0f, -90.0f, &streak), 0);
    expect_int("next_busy free dead band stays free",
               daemon_monitoring_cad_next_busy(0, -91.0f, -90.0f, &streak), 0);
    expect_int("next_busy free keeps streak zero", streak, 0);
}

static void test_next_busy_free_confirmation(void)
{
    int streak = 0;

    expect_int("next_busy first clearly-free stays busy",
               daemon_monitoring_cad_next_busy(1, -95.0f, -90.0f, &streak), 1);
    expect_int("next_busy first clearly-free streak", streak, 1);
    expect_int("next_busy second clearly-free confirms",
               daemon_monitoring_cad_next_busy(1, -95.0f, -90.0f, &streak), 0);
    expect_int("next_busy confirm resets streak", streak, 0);

    // Boundary: exactly threshold - 3 dB counts as clearly free.
    streak = 0;
    expect_int("next_busy boundary clearly-free stays busy",
               daemon_monitoring_cad_next_busy(1, -93.0f, -90.0f, &streak), 1);
    expect_int("next_busy boundary clearly-free confirms",
               daemon_monitoring_cad_next_busy(1, -93.0f, -90.0f, &streak), 0);
}

static void test_next_busy_dead_band_cancels(void)
{
    int streak = 0;

    expect_int("next_busy pending confirmation stays busy",
               daemon_monitoring_cad_next_busy(1, -95.0f, -90.0f, &streak), 1);
    expect_int("next_busy dead band retains busy",
               daemon_monitoring_cad_next_busy(1, -91.0f, -90.0f, &streak), 1);
    expect_int("next_busy dead band cancels streak", streak, 0);
    expect_int("next_busy restart needs full confirmation",
               daemon_monitoring_cad_next_busy(1, -95.0f, -90.0f, &streak), 1);
    expect_int("next_busy restarted streak", streak, 1);
}

/* --- Monitor tick: edges and hysteresis ---------------------------------- */

static void test_tick_initial_free_is_silent(void)
{
    RadioController ctrl;

    init_ctrl(&ctrl, RADIO_HEALTH_READY, RADIO_MODE_LORA);

    expect_int("initial free no edge", tick_edge(&ctrl, -95.0f), 0);
    expect_int("initial free latch off",
               ctrl.cad_broadcast_active.load() ? 1 : 0, 0);
    expect_int("initial free again no edge", tick_edge(&ctrl, -95.0f), 0);
}

static void test_tick_free_to_busy_single_edge(void)
{
    RadioController ctrl;

    init_ctrl(&ctrl, RADIO_HEALTH_READY, RADIO_MODE_LORA);

    expect_int("free to busy rising edge", tick_edge(&ctrl, -80.0f), 1);
    expect_int("busy latch on", ctrl.cad_broadcast_active.load() ? 1 : 0, 1);
    expect_int("continued busy no duplicate", tick_edge(&ctrl, -80.0f), 0);
    expect_int("continued busy latch stays on",
               ctrl.cad_broadcast_active.load() ? 1 : 0, 1);
}

static void test_tick_busy_to_confirmed_free(void)
{
    RadioController ctrl;

    init_ctrl(&ctrl, RADIO_HEALTH_READY, RADIO_MODE_LORA);
    tick_edge(&ctrl, -80.0f);

    // First clearly-free sample: still busy (LED stays on), no edge yet.
    expect_int("first free sample no edge", tick_edge(&ctrl, -95.0f), 0);
    expect_int("pending confirmation latch stays on",
               ctrl.cad_broadcast_active.load() ? 1 : 0, 1);
    expect_int("pending confirmation streak",
               ctrl.cad_monitor_free_streak.load(), 1);

    // Second clearly-free sample confirms: exactly one falling edge.
    expect_int("second free sample falling edge", tick_edge(&ctrl, -95.0f), -1);
    expect_int("confirmed free latch off",
               ctrl.cad_broadcast_active.load() ? 1 : 0, 0);
    expect_int("continued free no duplicate", tick_edge(&ctrl, -95.0f), 0);
}

static void test_tick_rx_pending_does_not_suppress_cad0(void)
{
    RadioController ctrl;

    init_ctrl(&ctrl, RADIO_HEALTH_READY, RADIO_MODE_LORA);
    tick_edge(&ctrl, -80.0f);

    // Regression: a pending RX packet used to drop the CAD=0 broadcast while
    // the latch was cleared anyway, leaving clients stuck at CAD=1.
    ctrl.received.store(true);
    expect_int("rx pending first free sample no edge",
               tick_edge(&ctrl, -95.0f), 0);
    expect_int("rx pending falling edge still emitted",
               tick_edge(&ctrl, -95.0f), -1);
    expect_int("rx pending latch off",
               ctrl.cad_broadcast_active.load() ? 1 : 0, 0);
    expect_int("rx flag untouched by monitor",
               ctrl.received.load() ? 1 : 0, 1);
}

static void test_tick_dead_band_no_flicker(void)
{
    RadioController ctrl;

    init_ctrl(&ctrl, RADIO_HEALTH_READY, RADIO_MODE_LORA);

    // While free, dead-band energy must not raise CAD=1.
    expect_int("free dead band no edge", tick_edge(&ctrl, -91.0f), 0);
    expect_int("free dead band latch off",
               ctrl.cad_broadcast_active.load() ? 1 : 0, 0);

    // While busy, dead-band energy must not drop CAD=0.
    tick_edge(&ctrl, -80.0f);
    expect_int("busy dead band no edge", tick_edge(&ctrl, -91.0f), 0);
    expect_int("busy dead band latch on",
               ctrl.cad_broadcast_active.load() ? 1 : 0, 1);
    expect_int("busy dead band streak reset",
               ctrl.cad_monitor_free_streak.load(), 0);
}

static void test_tick_interrupted_confirmation(void)
{
    RadioController ctrl;

    init_ctrl(&ctrl, RADIO_HEALTH_READY, RADIO_MODE_LORA);
    tick_edge(&ctrl, -80.0f);

    // Busy energy interrupts an incomplete confirmation: no CAD=0.
    expect_int("interrupt: first free sample", tick_edge(&ctrl, -95.0f), 0);
    expect_int("interrupt: busy sample no edge", tick_edge(&ctrl, -85.0f), 0);
    expect_int("interrupt: streak cancelled",
               ctrl.cad_monitor_free_streak.load(), 0);
    expect_int("interrupt: latch still on",
               ctrl.cad_broadcast_active.load() ? 1 : 0, 1);

    // Dead-band noise likewise cancels the confirmation.
    expect_int("interrupt: free sample again", tick_edge(&ctrl, -95.0f), 0);
    expect_int("interrupt: dead band no edge", tick_edge(&ctrl, -91.0f), 0);
    expect_int("interrupt: streak cancelled again",
               ctrl.cad_monitor_free_streak.load(), 0);

    // Only a full uninterrupted confirmation emits the falling edge.
    expect_int("interrupt: confirmation restarts", tick_edge(&ctrl, -95.0f), 0);
    expect_int("interrupt: confirmation completes", tick_edge(&ctrl, -95.0f), -1);
}

static void test_tick_is_non_destructive(void)
{
    RadioController ctrl;

    init_ctrl(&ctrl, RADIO_HEALTH_READY, RADIO_MODE_LORA);

    tick_edge(&ctrl, -95.0f);
    tick_edge(&ctrl, -80.0f);
    tick_edge(&ctrl, -95.0f);
    tick_edge(&ctrl, -95.0f);

    // Monitoring must never touch RX or run active CAD scans.
    expect_int("monitor no scanChannel", fake(&ctrl)->scan_count, 0);
    expect_int("monitor no startReceive", fake(&ctrl)->start_receive_count, 0);
    expect_int("monitor no setPacketReceivedAction",
               fake(&ctrl)->callback_count, 0);
    expect_int("monitor no clearIrq", fake(&ctrl)->clear_irq_count, 0);
}

static void test_tick_unavailable_keeps_state(void)
{
    RadioController ctrl;
    DaemonCadMonitorTick tick;

    init_ctrl(&ctrl, RADIO_HEALTH_READY, RADIO_MODE_LORA);
    tick_edge(&ctrl, -80.0f);
    tick_edge(&ctrl, -95.0f);

    ctrl.mode = RADIO_MODE_FSK;
    fake(&ctrl)->rssi = -95.0f;
    tick = daemon_cad_monitor_tick(&ctrl);
    expect_int("unavailable not sampled", tick.sampled, 0);
    expect_int("unavailable no edge", tick.edge, 0);
    expect_int("unavailable latch unchanged",
               ctrl.cad_broadcast_active.load() ? 1 : 0, 1);
    expect_int("unavailable streak unchanged",
               ctrl.cad_monitor_free_streak.load(), 1);
}

static void test_tick_after_latch_reset(void)
{
    RadioController ctrl;

    init_ctrl(&ctrl, RADIO_HEALTH_READY, RADIO_MODE_LORA);
    tick_edge(&ctrl, -80.0f);
    tick_edge(&ctrl, -95.0f);

    // Disable/disconnect paths clear the latch and the streak without a
    // broadcast (SET CADMONITOR=0 and the unsubscribed stale-latch guard).
    ctrl.cad_broadcast_active.store(false);
    ctrl.cad_monitor_free_streak.store(0);

    // Re-subscribed on a free channel: silent, no unsolicited CAD=0.
    expect_int("after reset free is silent", tick_edge(&ctrl, -95.0f), 0);

    // Re-subscribed on a busy channel: exactly one fresh CAD=1.
    expect_int("after reset busy rises once", tick_edge(&ctrl, -80.0f), 1);
    expect_int("after reset no duplicate", tick_edge(&ctrl, -80.0f), 0);
}

/* --- M4 lock discipline: the monitoring tick must never block behind TX --- */

static void test_tick_skips_while_tx_busy(void)
{
    RadioController ctrl;

    init_ctrl(&ctrl, RADIO_HEALTH_READY, RADIO_MODE_LORA);
    tick_edge(&ctrl, -80.0f);          // latch busy first

    ctrl.tx_busy.store(true);
    fake(&ctrl)->rssi = -95.0f;        // would otherwise start free confirmation
    DaemonCadMonitorTick tick = daemon_cad_monitor_tick(&ctrl);

    expect_int("tx busy tick not sampled", tick.sampled, 0);
    expect_int("tx busy tick no edge", tick.edge, 0);
    expect_int("tx busy latch unchanged",
               ctrl.cad_broadcast_active.load() ? 1 : 0, 1);
    expect_int("tx busy streak unchanged",
               ctrl.cad_monitor_free_streak.load(), 0);

    ctrl.tx_busy.store(false);
}

static void test_tick_returns_while_mutex_held(void)
{
    RadioController ctrl;

    init_ctrl(&ctrl, RADIO_HEALTH_READY, RADIO_MODE_LORA);

    // Fake slow sender: hold radio_mutex like the worker does across the
    // blocking transmit(). The tick (main-loop path) must return immediately
    // via try_to_lock instead of stalling until release.
    std::unique_lock<std::recursive_mutex> hold(ctrl.radio_mutex);
    std::atomic<int> done(0);
    std::thread main_loop([&]() {
        DaemonCadMonitorTick tick = daemon_cad_monitor_tick(&ctrl);
        expect_int("held mutex tick not sampled", tick.sampled, 0);
        expect_int("held mutex tick no edge", tick.edge, 0);
        done.store(1);
    });

    // The tick must complete while the lock is STILL held here.
    for (int i = 0; i < 2000 && !done.load(); i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    expect_int("tick returned while mutex held", done.load(), 1);

    hold.unlock();
    main_loop.join();
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

    test_next_busy_asserts_immediately();
    test_next_busy_free_stays_free();
    test_next_busy_free_confirmation();
    test_next_busy_dead_band_cancels();
    test_tick_initial_free_is_silent();
    test_tick_free_to_busy_single_edge();
    test_tick_busy_to_confirmed_free();
    test_tick_rx_pending_does_not_suppress_cad0();
    test_tick_dead_band_no_flicker();
    test_tick_interrupted_confirmation();
    test_tick_is_non_destructive();
    test_tick_unavailable_keeps_state();
    test_tick_after_latch_reset();
    test_tick_skips_while_tx_busy();
    test_tick_returns_while_mutex_held();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
