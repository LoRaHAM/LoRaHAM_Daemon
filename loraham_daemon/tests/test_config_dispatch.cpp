#include "../config_dispatch.h"

#include <stdio.h>
#include <atomic>
#include <mutex>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/*
 * CONFIG dispatch tests.
 *
 * These lock the socket-ready/read/apply/restart path before the main loop
 * is split further for the event backend.
 */

static int g_ok = 0;
static int g_fail = 0;

/* Writing a command into the socketpair. GCC's warn_unused_result on write() is not silenced
   by a (void) cast, so the result is consumed here; what proves the command arrived is the
   assertion on the reply that follows each send. */
#define SEND(expr) do { ssize_t send_rc_ = (expr); (void)send_rc_; } while (0)

/* --- Fake radio --- */

// Fake driver: overrides the virtual RadioDriver delegates and counts calls
// exactly as the earlier template fake did (counter semantics unchanged).
struct FakeRadio : public RadioDriver {
    int callback_count;
    int clear_callback_count;
    int start_receive_count;
    int scan_result;
    int scan_count;
    float rssi;
    void (*last_callback)(void);

    FakeRadio() : RadioDriver(NULL),
                  callback_count(0), clear_callback_count(0), start_receive_count(0),
                  scan_result(0), scan_count(0), rssi(-82.5f),
                  last_callback(NULL) {}

    void setPacketReceivedAction(void (*cb)(void)) override
    {
        last_callback = cb;
        callback_count++;
    }

    /* The base class forwards to phy_, which is NULL in these fakes: the CAD
     * probe now takes the RX alert off DIO0 before scanning, so every fake
     * driver needs this or the call dereferences null. */
    void clearPacketReceivedAction() override
    {
        clear_callback_count++;
        last_callback = NULL;
    }

    int16_t startReceive() override
    {
        start_receive_count++;
        return 0;
    }

    int16_t clearIrq(uint32_t) override
    {
        return 0;
    }

    int16_t scanChannel() override
    {
        scan_count++;
        return scan_result;
    }

    /* Counted: radio_controller_packet_rssi() reaches the driver through here, and it takes
     * radio_mutex with a BLOCKING lock_guard. No main-loop path may do that (radio_controller.h),
     * so the periodic NOSCAN read must never call it. Counting is the only way a test can see
     * that -- a bare return value is invisible. */
    int get_rssi_count = 0;
    float getRSSI() override
    {
        get_rssi_count++;
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
    /* Deterministic drain-during-format hook. The CHANNEL formatter reads the live RSSI AFTER
     * its caller has already decided whether a packet is pending, so clearing `received` here
     * reproduces the real race -- the RX drain landing between the decision and the reply --
     * without any timing dependence. */
    std::atomic<bool> *drain_on_live_rssi = nullptr;

    // No Module behind the fake: live RSSI unavailable as before (-200).
    float readLiveRssi(RadioMode_t, bool) override
    {
        if (drain_on_live_rssi)
            drain_on_live_rssi->store(false);
        return -200.0f;
    }
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

/* --- Test state --- */

typedef struct {
    int calls;
    char tag[32];
    char cmd[128];
    /* 0 (memset default) maps to CONFIG_APPLY_APPLIED so the existing
     * re-arm assertions keep exercising the radio-touched path. */
    int result_override; /* 0=APPLIED, else the ConfigApplyStatus to return */
} ApplyState;

static ApplyState g_apply_state;


/* --- Async TX runtime status stubs -------------------------------------- */

static size_t g_stub_pending = 0;
static int g_stub_job_active = 0;

size_t daemon_tx_async_runtime_pending()
{
    return g_stub_pending;
}

int daemon_tx_async_runtime_job_active()
{
    return g_stub_job_active;
}

size_t daemon_tx_async_runtime_dropped()
{
    return 0;
}

size_t daemon_tx_async_runtime_rejected()
{
    return 0;
}

size_t daemon_tx_async_runtime_processed()
{
    return 0;
}

size_t daemon_tx_async_runtime_completion_stale()
{
    return 0;
}


size_t daemon_tx_async_runtime_completion_dropped()
{
    return 0;
}

int daemon_tx_async_runtime_last_result(DaemonTxJobResult *out)
{
    (void)out;
    return 0;
}

static void fake_rx_callback(void)
{
}

/* --- Test helpers --- */

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

static void expect_str(const char *name, const char *actual, const char *expected)
{
    if (strcmp(actual, expected) == 0) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: expected '%s', got '%s'\n", name, expected, actual);
    }
}

static void expect_contains(const char *name, const char *actual, const char *needle)
{
    if (actual && needle && strstr(actual, needle)) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: expected substring '%s', got '%s'\n",
               name, needle ? needle : "", actual ? actual : "");
    }
}

static int make_socket_pair(int sv[2])
{
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        perror("socketpair");
        return 0;
    }

    return 1;
}

/* --- Apply callback used by tests --- */

static ConfigApplyStatus record_apply_config(RadioDriver& radio,
                                             const char *tag,
                                             const char *cmd,
                                             RadioMode_t& mode,
                                             std::atomic<bool>& getrssi_active)
{
    (void)radio;

    g_apply_state.calls++;
    snprintf(g_apply_state.tag, sizeof(g_apply_state.tag), "%s", tag);
    snprintf(g_apply_state.cmd, sizeof(g_apply_state.cmd), "%s", cmd);

    mode = RADIO_MODE_FSK;
    getrssi_active.store(true);

    if (g_apply_state.result_override != 0)
        return (ConfigApplyStatus)g_apply_state.result_override;

    return CONFIG_APPLY_APPLIED;
}

static void init_fake_controller(RadioController *ctrl,
                                 RadioHealth health)
{
    radio_controller_init(ctrl,
                          RADIO_BAND_433,
                          "TEST",
                          false,
                          fake_rx_callback,
                          13);
    ctrl->driver.reset(new FakeRadio());
    ctrl->health = health;
}

static ConfigDispatchContext make_context(ClientSlot *slots,
                                          RadioController *ctrl)
{
    ConfigDispatchLog log = {
        NULL,
        NULL,
        NULL
    };

    ConfigDispatchContext ctx = {
        slots,
        ctrl,
        "CONF TEST",
        record_apply_config,
        log
    };

    return ctx;
}

/* --- Dispatch behavior --- */

static void test_dispatch_ready_client(void)
{
    int sv[2];
    ClientSlot slots[2];
    uint8_t buf[buf_SIZE];
    EventLoopSet set;
    EventLoopReadySet readfds;
    RadioController ctrl;
    init_fake_controller(&ctrl, RADIO_HEALTH_READY);

    memset(&g_apply_state, 0, sizeof(g_apply_state));
    client_slot_init_all(slots, 2);
    memset(buf, 0, sizeof(buf));

    if (!make_socket_pair(sv)) {
        g_fail++;
        return;
    }

    client_slot_set_fd(&slots[0], sv[1]);

    if (event_loop_init(&set) != 0) {
        close(sv[0]);
        client_slot_close(&slots[0]);
        g_fail++;
        printf("[FAIL] config dispatch init\n");
        return;
    }

    const char *cmd = "SET GETRSSI=1\n";
    SEND(write(sv[0], cmd, strlen(cmd)));

    event_loop_reset(&set);
    event_loop_add_fd(&set, sv[1]);
    expect_int("ready wait", event_loop_wait(&set, &readfds, 100000), 1);

    ConfigDispatchContext ctx =
        make_context(slots, &ctrl);

    config_dispatch_context(&ctx, 2, &readfds, buf);

    expect_int("apply called once", g_apply_state.calls, 1);
    expect_str("apply tag", g_apply_state.tag, "CONF TEST");
    expect_str("apply cmd", g_apply_state.cmd, "SET GETRSSI=1");
    expect_int("mode updated by apply", ctrl.mode == RADIO_MODE_FSK, 1);
    expect_int("getrssi updated by apply", ctrl.getrssi_active.load() == true, 1);
    expect_int("callback restored", fake(&ctrl)->callback_count, 1);
    expect_int("startReceive called", fake(&ctrl)->start_receive_count, 1);
    expect_int("client still open", client_slot_has_client(&slots[0]), 1);

    event_loop_close(&set);
    close(sv[0]);
    client_slot_close(&slots[0]);
}


static void test_dispatch_ready_client_epoll(void)
{
    int sv[2];
    ClientSlot slots[2];
    uint8_t buf[buf_SIZE];
    EventLoopSet set;
    EventLoopReadySet readfds;
    RadioController ctrl;
    init_fake_controller(&ctrl, RADIO_HEALTH_READY);

    memset(&g_apply_state, 0, sizeof(g_apply_state));
    client_slot_init_all(slots, 2);
    memset(buf, 0, sizeof(buf));

    if (!make_socket_pair(sv)) {
        g_fail++;
        return;
    }

    client_slot_set_fd(&slots[0], sv[1]);

    if (event_loop_init(&set) != 0) {
        close(sv[0]);
        client_slot_close(&slots[0]);
        g_fail++;
        printf("[FAIL] config epoll init\n");
        return;
    }

    const char *cmd = "SET GETRSSI=1\n";
    SEND(write(sv[0], cmd, strlen(cmd)));

    event_loop_add_fd(&set, sv[1]);
    expect_int("ready epoll wait", event_loop_wait(&set, &readfds, 100000), 1);

    ConfigDispatchContext ctx =
        make_context(slots, &ctrl);

    config_dispatch_context(&ctx, 2, &readfds, buf);

    expect_int("epoll apply called once", g_apply_state.calls, 1);
    expect_str("epoll apply cmd", g_apply_state.cmd, "SET GETRSSI=1");
    expect_int("epoll callback restored", fake(&ctrl)->callback_count, 1);
    expect_int("epoll startReceive called", fake(&ctrl)->start_receive_count, 1);

    event_loop_close(&set);
    close(sv[0]);
    client_slot_close(&slots[0]);
}

static void test_dispatch_ignores_not_ready_client(void)
{
    int sv[2];
    ClientSlot slots[2];
    uint8_t buf[buf_SIZE];
    EventLoopSet set;
    EventLoopReadySet readfds;
    RadioController ctrl;
    init_fake_controller(&ctrl, RADIO_HEALTH_FAILED);

    memset(&g_apply_state, 0, sizeof(g_apply_state));
    client_slot_init_all(slots, 2);
    memset(buf, 0, sizeof(buf));

    if (!make_socket_pair(sv)) {
        g_fail++;
        return;
    }

    client_slot_set_fd(&slots[0], sv[1]);

    if (event_loop_init(&set) != 0) {
        close(sv[0]);
        client_slot_close(&slots[0]);
        g_fail++;
        printf("[FAIL] config dispatch not-ready init\n");
        return;
    }

    const char *cmd = "SET GETRSSI=1\n";
    SEND(write(sv[0], cmd, strlen(cmd)));

    event_loop_reset(&set);
    event_loop_add_fd(&set, sv[1]);
    expect_int("not ready wait", event_loop_wait(&set, &readfds, 100000), 1);

    ConfigDispatchContext ctx =
        make_context(slots, &ctrl);

    config_dispatch_context(&ctx, 2, &readfds, buf);

    expect_int("not ready apply count", g_apply_state.calls, 0);
    expect_int("not ready callback count", fake(&ctrl)->callback_count, 0);
    expect_int("not ready startReceive count", fake(&ctrl)->start_receive_count, 0);
    expect_int("not ready client open", client_slot_has_client(&slots[0]), 1);

    event_loop_close(&set);
    close(sv[0]);
    client_slot_close(&slots[0]);
}



static void test_status_uses_cad_broadcast_latch(void)
{
    RadioController ctrl;
    /* The production dispatcher formats into 512 bytes (config_dispatch.cpp);
     * with every counter at its widest the line is ~450 bytes, so a smaller
     * buffer here would truncate silently and the newline assertion below
     * would be the only thing to notice. */
    char status[512];

    init_fake_controller(&ctrl, RADIO_HEALTH_READY);

    ctrl.cad_active.store(true);
    ctrl.cad_broadcast_active.store(false);
    config_status_format(status, sizeof(status), &ctrl);
    expect_contains("status ignores transient CAD", status, " CAD=0 ");

    ctrl.cad_active.store(false);
    ctrl.cad_broadcast_active.store(true);
    config_status_format(status, sizeof(status), &ctrl);
    expect_contains("status reports broadcast CAD", status, " CAD=1 ");
    expect_contains("status reports queue rejects", status, " TXQREJECT=0 ");
}

/* HIGHPOWER= and CHIPFAMILY= are additive fields at the END of the line, read
 * off the controller's witnesses, never off anything a client can set. */
static void test_status_reports_high_power_and_family(void)
{
    RadioController ctrl;
    char status[512];

    init_fake_controller(&ctrl, RADIO_HEALTH_READY);

    config_status_format(status, sizeof(status), &ctrl);
    expect_contains("status default HIGHPOWER=0", status, " HIGHPOWER=0 ");
    expect_contains("status default family sx127x, last field, newline",
                    status, " CHIPFAMILY=SX127x\n");

    ctrl.high_power_enabled = true;
    ctrl.chip_family = DAEMON_CHIP_FAMILY_SX1262;
    config_status_format(status, sizeof(status), &ctrl);
    expect_contains("status HIGHPOWER=1 is the raw flag", status, " HIGHPOWER=1 ");
    expect_contains("status family sx1262", status, " CHIPFAMILY=SX1262\n");

    /* Every counter at its widest must still end in the newline: a truncated
     * STATUS is a silent protocol break. */
    ctrl.cad_wait_timeout_ms.store(4294967295u);
    ctrl.cad_idle_stable_ms.store(4294967295u);
    ctrl.cad_poll_interval_ms.store(4294967295u);
    ctrl.cad_rssi_threshold_dbm.store(-130.0f);
    config_status_format(status, sizeof(status), &ctrl);
    size_t n = strlen(status);
    expect_int("status at wide counters still ends in newline",
               n > 0 && status[n - 1] == '\n', 1);
    expect_int("status at wide counters fits the production buffer", n < 512, 1);
}

static void test_dispatch_get_channel_restores_rx(void)
{
    int sv[2];
    ClientSlot slots[2];
    uint8_t buf[buf_SIZE];
    EventLoopSet set;
    EventLoopReadySet readfds;
    RadioController ctrl;
    char out[256];
    ssize_t n;

    init_fake_controller(&ctrl, RADIO_HEALTH_READY);
    fake(&ctrl)->scan_result = 1;
    fake(&ctrl)->rssi = -77.25f;

    memset(&g_apply_state, 0, sizeof(g_apply_state));
    client_slot_init_all(slots, 2);
    memset(buf, 0, sizeof(buf));
    memset(out, 0, sizeof(out));

    if (!make_socket_pair(sv)) {
        g_fail++;
        return;
    }

    client_slot_set_fd(&slots[0], sv[1]);

    if (event_loop_init(&set) != 0) {
        close(sv[0]);
        client_slot_close(&slots[0]);
        g_fail++;
        printf("[FAIL] config GET CHANNEL init\n");
        return;
    }

    const char *cmd = "GET CHANNEL\n";
    SEND(write(sv[0], cmd, strlen(cmd)));

    event_loop_reset(&set);
    event_loop_add_fd(&set, sv[1]);
    expect_int("get channel wait", event_loop_wait(&set, &readfds, 100000), 1);

    ConfigDispatchContext ctx =
        make_context(slots, &ctrl);

    config_dispatch_context(&ctx, 2, &readfds, buf);

    n = read(sv[0], out, sizeof(out) - 1);
    if (n < 0)
        n = 0;
    out[n] = '\0';

    expect_int("get channel apply count", g_apply_state.calls, 0);
    expect_int("get channel scan count", fake(&ctrl)->scan_count, 1);
    expect_contains("get channel prefix", out, "CHANNEL RADIO=READY");
    expect_contains("get channel busy", out, "BUSY=1");
    expect_contains("get channel cad", out, "CAD=1");
    expect_contains("get channel cadscan", out, "CADSCAN=1");
    expect_contains("get channel cadstate", out, "CADSTATE=BUSY");
    expect_contains("get channel rssi", out, "RSSI=-77.25");
    expect_contains("get channel packet rssi", out, "PACKETRSSI=-77.25");
    expect_contains("get channel live rssi", out, "LIVERSSI=-200.00");
    expect_contains("get channel mode", out, "MODE=LORA");
    expect_contains("get channel txmode", out, "TXMODE=MANAGED");
    expect_int("get channel callback restored", fake(&ctrl)->callback_count, 1);
    expect_int("get channel startReceive called", fake(&ctrl)->start_receive_count, 1);
    expect_int("get channel client open", client_slot_has_client(&slots[0]), 1);

    event_loop_close(&set);
    close(sv[0]);
    client_slot_close(&slots[0]);
}


static void test_dispatch_get_channel_during_tx_skips_scan(void)
{
    int sv[2];
    ClientSlot slots[2];
    uint8_t buf[buf_SIZE];
    EventLoopSet set;
    EventLoopReadySet readfds;
    RadioController ctrl;
    char out[256];
    ssize_t n;

    init_fake_controller(&ctrl, RADIO_HEALTH_READY);
    ctrl.tx_busy.store(true);

    memset(&g_apply_state, 0, sizeof(g_apply_state));
    client_slot_init_all(slots, 2);
    memset(buf, 0, sizeof(buf));
    memset(out, 0, sizeof(out));

    if (!make_socket_pair(sv)) {
        g_fail++;
        return;
    }

    client_slot_set_fd(&slots[0], sv[1]);

    if (event_loop_init(&set) != 0) {
        close(sv[0]);
        client_slot_close(&slots[0]);
        g_fail++;
        printf("[FAIL] config GET CHANNEL busy init\n");
        return;
    }

    SEND(write(sv[0], "GET CHANNEL\n", strlen("GET CHANNEL\n")));

    event_loop_reset(&set);
    event_loop_add_fd(&set, sv[1]);
    expect_int("get channel busy wait",
               event_loop_wait(&set, &readfds, 100000),
               1);

    ConfigDispatchContext ctx =
        make_context(slots, &ctrl);

    config_dispatch_context(&ctx, 2, &readfds, buf);

    n = read(sv[0], out, sizeof(out) - 1);
    if (n < 0)
        n = 0;
    out[n] = '\0';

    expect_int("get channel busy apply count", g_apply_state.calls, 0);
    expect_int("get channel busy no scan", fake(&ctrl)->scan_count, 0);
    expect_contains("get channel busy marker", out, "BUSY=1");
    expect_contains("get channel busy CAD marker", out, "CAD=0");
    expect_contains("get channel busy scan marker", out, "CADSCAN=0");
    expect_contains("get channel busy state", out, "CADSTATE=UNAVAILABLE");
    expect_int("get channel busy callback untouched",
               fake(&ctrl)->callback_count,
               0);
    expect_int("get channel busy RX untouched",
               fake(&ctrl)->start_receive_count,
               0);
    expect_int("get channel busy client open",
               client_slot_has_client(&slots[0]),
               1);

    event_loop_close(&set);
    close(sv[0]);
    client_slot_close(&slots[0]);
}

static void test_dispatch_set_txqueue(void)
{
    int sv[2];
    ClientSlot slots[2];
    uint8_t buf[buf_SIZE];
    EventLoopSet set;
    EventLoopReadySet readfds;
    RadioController ctrl;
    init_fake_controller(&ctrl, RADIO_HEALTH_READY);

    memset(&g_apply_state, 0, sizeof(g_apply_state));
    client_slot_init_all(slots, 2);
    memset(buf, 0, sizeof(buf));

    if (!make_socket_pair(sv)) {
        g_fail++;
        return;
    }

    client_slot_set_fd(&slots[0], sv[1]);

    if (event_loop_init(&set) != 0) {
        close(sv[0]);
        client_slot_close(&slots[0]);
        g_fail++;
        printf("[FAIL] config TXQUEUE init\n");
        return;
    }

    const char *cmd = "SET TXQUEUE=1\n";
    SEND(write(sv[0], cmd, strlen(cmd)));

    event_loop_reset(&set);
    event_loop_add_fd(&set, sv[1]);
    expect_int("txqueue wait", event_loop_wait(&set, &readfds, 100000), 1);

    ConfigDispatchContext ctx =
        make_context(slots, &ctrl);

    config_dispatch_context(&ctx, 2, &readfds, buf);

    expect_int("txqueue apply not called", g_apply_state.calls, 0);
    expect_int("txqueue enabled", ctrl.tx_queue_active.load() == true, 1);
    expect_int("txqueue callback not restored", fake(&ctrl)->callback_count, 0);
    expect_int("txqueue startReceive not called", fake(&ctrl)->start_receive_count, 0);
    expect_int("txqueue client still open", client_slot_has_client(&slots[0]), 1);

    event_loop_close(&set);
    close(sv[0]);
    client_slot_close(&slots[0]);
}

static void test_dispatch_set_txresult(void)
{
    int sv[2];
    ClientSlot slots[2];
    uint8_t buf[buf_SIZE];
    EventLoopSet set;
    EventLoopReadySet readfds;
    RadioController ctrl;
    init_fake_controller(&ctrl, RADIO_HEALTH_READY);

    memset(&g_apply_state, 0, sizeof(g_apply_state));
    client_slot_init_all(slots, 2);
    memset(buf, 0, sizeof(buf));

    if (!make_socket_pair(sv)) {
        g_fail++;
        return;
    }

    client_slot_set_fd(&slots[0], sv[1]);

    if (event_loop_init(&set) != 0) {
        close(sv[0]);
        client_slot_close(&slots[0]);
        g_fail++;
        printf("[FAIL] config TXRESULT init\n");
        return;
    }

    const char *cmd = "SET TXRESULT=1\n";
    SEND(write(sv[0], cmd, strlen(cmd)));

    event_loop_reset(&set);
    event_loop_add_fd(&set, sv[1]);
    expect_int("txresult wait", event_loop_wait(&set, &readfds, 100000), 1);

    ConfigDispatchContext ctx =
        make_context(slots, &ctrl);

    config_dispatch_context(&ctx, 2, &readfds, buf);

    expect_int("txresult apply not called", g_apply_state.calls, 0);
    expect_int("txresult enabled", ctrl.tx_result_active.load() == true, 1);
    expect_int("txresult callback not restored", fake(&ctrl)->callback_count, 0);
    expect_int("txresult startReceive not called", fake(&ctrl)->start_receive_count, 0);
    expect_int("txresult client still open", client_slot_has_client(&slots[0]), 1);

    event_loop_close(&set);
    close(sv[0]);
    client_slot_close(&slots[0]);
}


static void test_dispatch_sets_txmode_without_radio(void)
{
    int sv[2];
    ClientSlot slots[2];
    uint8_t buf[buf_SIZE];
    EventLoopSet set;
    EventLoopReadySet readfds;
    RadioController ctrl;
    init_fake_controller(&ctrl, RADIO_HEALTH_FAILED);

    memset(&g_apply_state, 0, sizeof(g_apply_state));
    client_slot_init_all(slots, 2);
    memset(buf, 0, sizeof(buf));

    if (!make_socket_pair(sv)) {
        g_fail++;
        return;
    }

    client_slot_set_fd(&slots[0], sv[1]);

    if (event_loop_init(&set) != 0) {
        close(sv[0]);
        client_slot_close(&slots[0]);
        g_fail++;
        printf("[FAIL] config dispatch txmode init\n");
        return;
    }

    const char *cmd = "SET TXMODE=DIRECT\n";
    SEND(write(sv[0], cmd, strlen(cmd)));

    event_loop_reset(&set);
    event_loop_add_fd(&set, sv[1]);
    expect_int("txmode wait", event_loop_wait(&set, &readfds, 100000), 1);

    ConfigDispatchContext ctx =
        make_context(slots, &ctrl);

    config_dispatch_context(&ctx, 2, &readfds, buf);

    expect_int("txmode apply count", g_apply_state.calls, 0);
    expect_int("txmode direct", ctrl.tx_mode == RADIO_TX_MODE_DIRECT, 1);
    expect_int("txmode client open", client_slot_has_client(&slots[0]), 1);

    event_loop_close(&set);
    close(sv[0]);
    client_slot_close(&slots[0]);
}

static void test_get_channel_pending_rx_guard(void)
{
    RadioController ctrl;
    char ch[256];

    init_fake_controller(&ctrl, RADIO_HEALTH_READY);

    // Idle: GET CHANNEL runs the active scan.
    ctrl.received.store(false);
    config_status_format_channel(ch, sizeof(ch), &ctrl);
    expect_int("get channel scans when idle", fake(&ctrl)->scan_count >= 1 ? 1 : 0, 1);

    // Pending RX: must NOT scan (would discard the packet) and report PENDING.
    int before = fake(&ctrl)->scan_count;
    ctrl.received.store(true);
    config_status_format_channel(ch, sizeof(ch), &ctrl);
    expect_int("get channel pending no scan", fake(&ctrl)->scan_count, before);
    expect_int("get channel pending state",
               strstr(ch, "CADSTATE=PENDING") != NULL ? 1 : 0, 1);
    expect_int("get channel pending cadscan 0",
               strstr(ch, "CADSCAN=0") != NULL ? 1 : 0, 1);
}

static void test_get_channel_noscan_never_scans(void)
{
    RadioController ctrl;
    char ch[256];

    init_fake_controller(&ctrl, RADIO_HEALTH_READY);

    // Idle: NOSCAN must NOT scan, and must say so rather than imply a verdict.
    ctrl.received.store(false);
    int before = fake(&ctrl)->scan_count;
    config_status_format_channel_passive(ch, sizeof(ch), &ctrl);
    expect_int("noscan idle no scan", fake(&ctrl)->scan_count, before);
    expect_int("noscan idle state",
               strstr(ch, "CADSTATE=NOTSCANNED") != NULL ? 1 : 0, 1);
    expect_int("noscan idle cadscan 0",
               strstr(ch, "CADSCAN=0") != NULL ? 1 : 0, 1);
    expect_int("noscan idle cad 0", strstr(ch, "CAD=0") != NULL ? 1 : 0, 1);
    // It still carries the fields a periodic reader actually wants.
    expect_int("noscan reports liverssi", strstr(ch, "LIVERSSI=") != NULL ? 1 : 0, 1);
    expect_int("noscan reports mode", strstr(ch, "MODE=") != NULL ? 1 : 0, 1);

    // The intersection case: a completed, undrained packet is strictly more informative than
    // "no verdict was requested", so PENDING wins -- and still without scanning.
    before = fake(&ctrl)->scan_count;
    ctrl.received.store(true);
    config_status_format_channel_passive(ch, sizeof(ch), &ctrl);
    expect_int("noscan pending no scan", fake(&ctrl)->scan_count, before);
    expect_int("noscan pending beats notscanned",
               strstr(ch, "CADSTATE=PENDING") != NULL ? 1 : 0, 1);
    expect_int("noscan pending cadscan 0",
               strstr(ch, "CADSCAN=0") != NULL ? 1 : 0, 1);
}

static void test_get_channel_noscan_null_controller(void)
{
    char ch[256];

    // config_dispatch answers GET CHANNEL *before* any readiness gate, so the formatter can be
    // reached with no controller at all. As an inner branch this code was safe only because of
    // the enclosing `ctrl && ctrl->received` test; hoisted, it must guard itself. Without that
    // guard this dereferences null and takes the daemon down.
    memset(ch, 0, sizeof(ch));
    config_status_format_channel_passive(ch, sizeof(ch), NULL);
    expect_int("noscan null ctrl emits a line", strncmp(ch, "CHANNEL ", 8) == 0 ? 1 : 0, 1);
    expect_int("noscan null ctrl cadscan 0",
               strstr(ch, "CADSCAN=0") != NULL ? 1 : 0, 1);
    expect_int("noscan null ctrl notscanned",
               strstr(ch, "CADSTATE=NOTSCANNED") != NULL ? 1 : 0, 1);
    expect_int("noscan null ctrl newline terminated",
               ch[strlen(ch) - 1] == '\n' ? 1 : 0, 1);
}

static void test_dispatch_get_channel_noscan_touches_nothing(void)
{
    int sv[2];
    ClientSlot slots[2];
    uint8_t buf[buf_SIZE];
    EventLoopSet set;
    EventLoopReadySet readfds;
    RadioController ctrl;
    char out[256];
    ssize_t n;

    init_fake_controller(&ctrl, RADIO_HEALTH_READY);
    fake(&ctrl)->scan_result = 1;          /* a scan, if one ran, would report BUSY */
    fake(&ctrl)->rssi = -77.25f;

    memset(&g_apply_state, 0, sizeof(g_apply_state));
    client_slot_init_all(slots, 2);
    memset(buf, 0, sizeof(buf));
    memset(out, 0, sizeof(out));

    if (!make_socket_pair(sv)) {
        g_fail++;
        return;
    }

    client_slot_set_fd(&slots[0], sv[1]);

    if (event_loop_init(&set) != 0) {
        close(sv[0]);
        client_slot_close(&slots[0]);
        g_fail++;
        printf("[FAIL] config GET CHANNEL NOSCAN init\n");
        return;
    }

    /* Through the real CONF socket, not just the formatter: the whole point of the command is
     * that a client can poll it, so the dispatch path is what has to be proven harmless. */
    const char *cmd = "GET CHANNEL NOSCAN\n";
    SEND(write(sv[0], cmd, strlen(cmd)));

    event_loop_reset(&set);
    event_loop_add_fd(&set, sv[1]);
    expect_int("noscan dispatch wait", event_loop_wait(&set, &readfds, 100000), 1);

    ConfigDispatchContext ctx =
        make_context(slots, &ctrl);

    config_dispatch_context(&ctx, 2, &readfds, buf);

    n = read(sv[0], out, sizeof(out) - 1);
    if (n < 0)
        n = 0;
    out[n] = '\0';

    /* THE point of the command: no mode-changing or destructive radio operation, and no CAD.
     * The live-RSSI read is still performed -- it is a passive register read behind a tx_busy
     * gate and a try_to_lock, which is what makes it safe on the main loop. */
    expect_int("noscan dispatch: no scan", fake(&ctrl)->scan_count, 0);
    expect_int("noscan dispatch: no startReceive", fake(&ctrl)->start_receive_count, 0);
    expect_int("noscan dispatch: no callback re-register", fake(&ctrl)->callback_count, 0);
    expect_int("noscan dispatch: no standby", fake(&ctrl)->standby_count, 0);
    expect_int("noscan dispatch: no callback clear", fake(&ctrl)->clear_callback_count, 0);
    expect_int("noscan dispatch: no config apply", g_apply_state.calls, 0);

    /* It still answers, as one data line, with the fields a periodic reader wants. */
    expect_contains("noscan dispatch prefix", out, "CHANNEL RADIO=READY");
    expect_contains("noscan dispatch cad", out, "CAD=0");
    expect_contains("noscan dispatch cadscan", out, "CADSCAN=0");
    expect_contains("noscan dispatch cadstate", out, "CADSTATE=NOTSCANNED");
    expect_contains("noscan dispatch live rssi", out, "LIVERSSI=-200.00");
    expect_contains("noscan dispatch mode", out, "MODE=LORA");
    expect_contains("noscan dispatch txmode", out, "TXMODE=MANAGED");
    expect_int("noscan dispatch: single line", strchr(out, '\n') == out + strlen(out) - 1 ? 1 : 0, 1);
    expect_int("noscan dispatch client open", client_slot_has_client(&slots[0]), 1);

    event_loop_close(&set);
    close(sv[0]);
    client_slot_close(&slots[0]);
}

static void test_legacy_channel_pending_survives_a_drain(void)
{
    RadioController ctrl;
    char ch[256];

    init_fake_controller(&ctrl, RADIO_HEALTH_READY);

    /* Legacy GET CHANNEL tests `received`, commits to its non-scanning branch, and has ALWAYS
     * answered PENDING unconditionally from there. If the packet is drained while the reply is
     * being formatted, it must still answer PENDING: NOTSCANNED is a state this command has never
     * emitted and no existing client expects, so producing it here would break the additive
     * contract. A formatter that re-reads `received` instead of consuming the caller's snapshot
     * answers NOTSCANNED and fails this test. */
    ctrl.received.store(true);
    fake(&ctrl)->drain_on_live_rssi = &ctrl.received;
    int before = fake(&ctrl)->scan_count;
    config_status_format_channel(ch, sizeof(ch), &ctrl);
    fake(&ctrl)->drain_on_live_rssi = nullptr;

    expect_int("legacy pending drained: still no scan", fake(&ctrl)->scan_count, before);
    expect_int("legacy pending drained: stays PENDING",
               strstr(ch, "CADSTATE=PENDING") != NULL ? 1 : 0, 1);
    expect_int("legacy pending drained: never NOTSCANNED",
               strstr(ch, "NOTSCANNED") == NULL ? 1 : 0, 1);
    expect_int("legacy pending drained: the drain really happened",
               ctrl.received.load() ? 1 : 0, 0);

    /* The same snapshot discipline on the NOSCAN side: decided once, then formatted. */
    ctrl.received.store(true);
    fake(&ctrl)->drain_on_live_rssi = &ctrl.received;
    config_status_format_channel_passive(ch, sizeof(ch), &ctrl);
    fake(&ctrl)->drain_on_live_rssi = nullptr;
    expect_int("noscan pending drained: stays PENDING",
               strstr(ch, "CADSTATE=PENDING") != NULL ? 1 : 0, 1);
}

static void test_noscan_never_blocks_on_a_transmit(void)
{
    RadioController ctrl;
    char ch[256];

    init_fake_controller(&ctrl, RADIO_HEALTH_READY);

    /* radio_controller.h's access rule: the TX worker may hold radio_mutex across the blocking
     * transmit() -- seconds at SF12 -- so NO main-loop path may block on that mutex. NOSCAN is
     * dispatched on the main loop and is the one command a client polls.
     *
     * The assertion that carries this is get_rssi_count: radio_controller_packet_rssi() reaches
     * the driver through getRSSI() and takes radio_mutex with a blocking lock_guard, so the ONLY
     * way NOSCAN is safe here is by never calling it. Do NOT try to prove this by holding
     * radio_mutex in the test and checking the call returns: radio_mutex is RECURSIVE, so a
     * same-thread lock_guard re-enters and returns immediately. Such a test passes on the
     * blocking implementation too and proves nothing. */
    ctrl.tx_busy.store(true);
    ctrl.received.store(false);

    int scans = fake(&ctrl)->scan_count;
    int rearms = fake(&ctrl)->start_receive_count;
    int rssi_calls = fake(&ctrl)->get_rssi_count;

    config_status_format_channel_passive(ch, sizeof(ch), &ctrl);

    ctrl.tx_busy.store(false);

    expect_int("noscan under TX: answered", strncmp(ch, "CHANNEL ", 8) == 0 ? 1 : 0, 1);
    expect_int("noscan under TX: never takes the blocking packet-RSSI path",
               fake(&ctrl)->get_rssi_count, rssi_calls);
    expect_int("noscan under TX: no scan", fake(&ctrl)->scan_count, scans);
    expect_int("noscan under TX: no re-arm", fake(&ctrl)->start_receive_count, rearms);
    expect_int("noscan under TX: no verdict claimed",
               strstr(ch, "CADSCAN=0") != NULL ? 1 : 0, 1);
    expect_int("noscan under TX: reports the busy transmit",
               strstr(ch, "BUSY=1") != NULL ? 1 : 0, 1);
    /* tx_busy short-circuits the live read, so the sentinel is what a caller sees. */
    expect_contains("noscan under TX: live rssi sentinel", ch, "LIVERSSI=-200.00");

    /* And on the ordinary idle path too -- the blocking call must be absent always, not only
     * while a transmit happens to be in flight. */
    rssi_calls = fake(&ctrl)->get_rssi_count;
    config_status_format_channel_passive(ch, sizeof(ch), &ctrl);
    expect_int("noscan idle: never takes the blocking packet-RSSI path",
               fake(&ctrl)->get_rssi_count, rssi_calls);
}

static void test_get_channel_command_matching_is_exact(void)
{
    // The two commands must never prefix-match each other in either direction, or a NOSCAN poll
    // silently becomes a scanning poll and the whole repair is undone.
    expect_int("plain matches plain", config_status_is_get_channel("GET CHANNEL"), 1);
    expect_int("plain rejects noscan", config_status_is_get_channel("GET CHANNEL NOSCAN"), 0);
    expect_int("noscan matches noscan",
               config_status_is_get_channel_noscan("GET CHANNEL NOSCAN"), 1);
    expect_int("noscan rejects plain", config_status_is_get_channel_noscan("GET CHANNEL"), 0);
    // Matching is on a trimmed, uppercased copy, like every other command.
    expect_int("noscan case insensitive",
               config_status_is_get_channel_noscan("get channel noscan"), 1);
    expect_int("noscan trims",
               config_status_is_get_channel_noscan("  GET CHANNEL NOSCAN  "), 1);
    expect_int("noscan rejects trailing junk",
               config_status_is_get_channel_noscan("GET CHANNEL NOSCANX"), 0);
}

static void test_set_cadrssi_parser(void)
{
    int dbm = 0;

    expect_int("cadrssi parse -85", config_status_is_set_cadrssi("SET CADRSSI=-85", &dbm), 1);
    expect_int("cadrssi value -85", dbm, -85);
    expect_int("cadrssi parse 0", config_status_is_set_cadrssi("SET CADRSSI=0", &dbm), 1);
    expect_int("cadrssi reject too low", config_status_is_set_cadrssi("SET CADRSSI=-131", &dbm), 0);
    expect_int("cadrssi reject positive", config_status_is_set_cadrssi("SET CADRSSI=5", &dbm), 0);
    expect_int("cadrssi reject junk", config_status_is_set_cadrssi("SET CADRSSI=-90x", &dbm), 0);
}

static void test_dispatch_sets_cadmonitor_optin(void)
{
    int sv[2];
    ClientSlot slots[2];
    uint8_t buf[buf_SIZE];
    EventLoopSet set;
    EventLoopReadySet readfds;
    RadioController ctrl;
    init_fake_controller(&ctrl, RADIO_HEALTH_FAILED);

    memset(&g_apply_state, 0, sizeof(g_apply_state));
    client_slot_init_all(slots, 2);
    memset(buf, 0, sizeof(buf));

    if (!make_socket_pair(sv)) {
        g_fail++;
        return;
    }

    client_slot_set_fd(&slots[0], sv[1]);

    if (event_loop_init(&set) != 0) {
        close(sv[0]);
        client_slot_close(&slots[0]);
        g_fail++;
        printf("[FAIL] config dispatch cadmonitor init\n");
        return;
    }

    ConfigDispatchContext ctx = make_context(slots, &ctrl);

    // Default: monitoring off.
    expect_int("cadmonitor default off", ctrl.cad_monitor_active.load() ? 1 : 0, 0);

    // SET CADMONITOR=1 enables the opt-in.
    const char *on = "SET CADMONITOR=1\n";
    SEND(write(sv[0], on, strlen(on)));
    event_loop_reset(&set);
    event_loop_add_fd(&set, sv[1]);
    expect_int("cadmonitor on wait", event_loop_wait(&set, &readfds, 100000), 1);
    config_dispatch_context(&ctx, 2, &readfds, buf);
    expect_int("cadmonitor enabled", ctrl.cad_monitor_active.load() ? 1 : 0, 1);

    // SET CADMONITOR=0 disables it and resets the broadcast latch and the
    // free-confirmation streak.
    ctrl.cad_broadcast_active.store(true);
    ctrl.cad_monitor_free_streak.store(1);
    const char *off = "SET CADMONITOR=0\n";
    SEND(write(sv[0], off, strlen(off)));
    event_loop_reset(&set);
    event_loop_add_fd(&set, sv[1]);
    expect_int("cadmonitor off wait", event_loop_wait(&set, &readfds, 100000), 1);
    config_dispatch_context(&ctx, 2, &readfds, buf);
    expect_int("cadmonitor disabled", ctrl.cad_monitor_active.load() ? 1 : 0, 0);
    expect_int("cadmonitor reset broadcast latch",
               ctrl.cad_broadcast_active.load() ? 1 : 0, 0);
    expect_int("cadmonitor reset free streak",
               ctrl.cad_monitor_free_streak.load(), 0);

    event_loop_close(&set);
    close(sv[0]);
    client_slot_close(&slots[0]);
}

static void test_dispatch_closes_eof_client(void)
{
    int sv[2];
    ClientSlot slots[2];
    uint8_t buf[buf_SIZE];
    EventLoopSet set;
    EventLoopReadySet readfds;
    RadioController ctrl;
    init_fake_controller(&ctrl, RADIO_HEALTH_READY);

    memset(&g_apply_state, 0, sizeof(g_apply_state));
    client_slot_init_all(slots, 2);
    memset(buf, 0, sizeof(buf));

    if (!make_socket_pair(sv)) {
        g_fail++;
        return;
    }

    client_slot_set_fd(&slots[0], sv[1]);
    client_output_queue_append(&slots[0].output, (const uint8_t *)"pending", 7);

    if (event_loop_init(&set) != 0) {
        close(sv[0]);
        client_slot_close(&slots[0]);
        g_fail++;
        printf("[FAIL] config dispatch eof init\n");
        return;
    }

    close(sv[0]);

    event_loop_reset(&set);
    event_loop_add_fd(&set, sv[1]);
    expect_int("eof wait", event_loop_wait(&set, &readfds, 100000), 1);

    ConfigDispatchContext ctx =
        make_context(slots, &ctrl);

    config_dispatch_context(&ctx, 2, &readfds, buf);

    expect_int("eof apply count", g_apply_state.calls, 0);
    expect_int("eof callback count", fake(&ctrl)->callback_count, 0);
    expect_int("eof startReceive count", fake(&ctrl)->start_receive_count, 0);
    expect_int("eof client closed", slots[0].fd, -1);
    expect_size("eof output reset", client_output_queue_pending(&slots[0].output), 0);
    expect_size("eof stream reset", slots[0].stream.len, 0);

    event_loop_close(&set);
}

static void test_dispatch_set_cadwait(void)
{
    int sv[2];
    ClientSlot slots[2];
    uint8_t buf[buf_SIZE];
    EventLoopSet set;
    EventLoopReadySet readfds;
    RadioController ctrl;
    init_fake_controller(&ctrl, RADIO_HEALTH_READY);

    memset(&g_apply_state, 0, sizeof(g_apply_state));
    client_slot_init_all(slots, 2);
    memset(buf, 0, sizeof(buf));

    if (!make_socket_pair(sv)) { g_fail++; return; }
    client_slot_set_fd(&slots[0], sv[1]);
    if (event_loop_init(&set) != 0) {
        close(sv[0]); client_slot_close(&slots[0]); g_fail++;
        printf("[FAIL] cadwait init\n"); return;
    }

    const char *cmd = "SET CADWAIT=300\n";
    SEND(write(sv[0], cmd, strlen(cmd)));
    event_loop_reset(&set);
    event_loop_add_fd(&set, sv[1]);
    expect_int("cadwait wait", event_loop_wait(&set, &readfds, 100000), 1);

    ConfigDispatchContext ctx = make_context(slots, &ctrl);
    config_dispatch_context(&ctx, 2, &readfds, buf);

    expect_int("cadwait apply not called",  g_apply_state.calls, 0);
    expect_int("cadwait stored", (int)ctrl.cad_wait_timeout_ms.load(), 300);
    expect_int("cadwait callback not restored",  fake(&ctrl)->callback_count, 0);
    expect_int("cadwait startReceive not called", fake(&ctrl)->start_receive_count, 0);
    expect_int("cadwait client still open", client_slot_has_client(&slots[0]), 1);

    event_loop_close(&set);
    close(sv[0]);
    client_slot_close(&slots[0]);
}

static void test_dispatch_set_cadidle(void)
{
    int sv[2];
    ClientSlot slots[2];
    uint8_t buf[buf_SIZE];
    EventLoopSet set;
    EventLoopReadySet readfds;
    RadioController ctrl;
    init_fake_controller(&ctrl, RADIO_HEALTH_READY);

    memset(&g_apply_state, 0, sizeof(g_apply_state));
    client_slot_init_all(slots, 2);
    memset(buf, 0, sizeof(buf));

    if (!make_socket_pair(sv)) { g_fail++; return; }
    client_slot_set_fd(&slots[0], sv[1]);
    if (event_loop_init(&set) != 0) {
        close(sv[0]); client_slot_close(&slots[0]); g_fail++;
        printf("[FAIL] cadidle init\n"); return;
    }

    const char *cmd = "SET CADIDLE=100\n";
    SEND(write(sv[0], cmd, strlen(cmd)));
    event_loop_reset(&set);
    event_loop_add_fd(&set, sv[1]);
    expect_int("cadidle wait", event_loop_wait(&set, &readfds, 100000), 1);

    ConfigDispatchContext ctx = make_context(slots, &ctrl);
    config_dispatch_context(&ctx, 2, &readfds, buf);

    expect_int("cadidle apply not called",  g_apply_state.calls, 0);
    expect_int("cadidle stored", (int)ctrl.cad_idle_stable_ms.load(), 100);
    expect_int("cadidle callback not restored",  fake(&ctrl)->callback_count, 0);
    expect_int("cadidle startReceive not called", fake(&ctrl)->start_receive_count, 0);
    expect_int("cadidle client still open", client_slot_has_client(&slots[0]), 1);

    event_loop_close(&set);
    close(sv[0]);
    client_slot_close(&slots[0]);
}

static void test_dispatch_set_cadpoll(void)
{
    int sv[2];
    ClientSlot slots[2];
    uint8_t buf[buf_SIZE];
    EventLoopSet set;
    EventLoopReadySet readfds;
    RadioController ctrl;
    init_fake_controller(&ctrl, RADIO_HEALTH_READY);

    memset(&g_apply_state, 0, sizeof(g_apply_state));
    client_slot_init_all(slots, 2);
    memset(buf, 0, sizeof(buf));

    if (!make_socket_pair(sv)) { g_fail++; return; }
    client_slot_set_fd(&slots[0], sv[1]);
    if (event_loop_init(&set) != 0) {
        close(sv[0]); client_slot_close(&slots[0]); g_fail++;
        printf("[FAIL] cadpoll init\n"); return;
    }

    const char *cmd = "SET CADPOLL=50\n";
    SEND(write(sv[0], cmd, strlen(cmd)));
    event_loop_reset(&set);
    event_loop_add_fd(&set, sv[1]);
    expect_int("cadpoll wait", event_loop_wait(&set, &readfds, 100000), 1);

    ConfigDispatchContext ctx = make_context(slots, &ctrl);
    config_dispatch_context(&ctx, 2, &readfds, buf);

    expect_int("cadpoll apply not called",  g_apply_state.calls, 0);
    expect_int("cadpoll stored", (int)ctrl.cad_poll_interval_ms.load(), 50);
    expect_int("cadpoll callback not restored",  fake(&ctrl)->callback_count, 0);
    expect_int("cadpoll startReceive not called", fake(&ctrl)->start_receive_count, 0);
    expect_int("cadpoll client still open", client_slot_has_client(&slots[0]), 1);

    event_loop_close(&set);
    close(sv[0]);
    client_slot_close(&slots[0]);
}

static void test_dispatch_set_cadtxaftertimeout(void)
{
    int sv[2];
    ClientSlot slots[2];
    uint8_t buf[buf_SIZE];
    EventLoopSet set;
    EventLoopReadySet readfds;
    RadioController ctrl;
    init_fake_controller(&ctrl, RADIO_HEALTH_READY);

    memset(&g_apply_state, 0, sizeof(g_apply_state));
    client_slot_init_all(slots, 2);
    memset(buf, 0, sizeof(buf));

    if (!make_socket_pair(sv)) { g_fail++; return; }
    client_slot_set_fd(&slots[0], sv[1]);
    if (event_loop_init(&set) != 0) {
        close(sv[0]); client_slot_close(&slots[0]); g_fail++;
        printf("[FAIL] cadtx init\n"); return;
    }

    const char *cmd = "SET CADTXAFTERTIMEOUT=1\n";
    SEND(write(sv[0], cmd, strlen(cmd)));
    event_loop_reset(&set);
    event_loop_add_fd(&set, sv[1]);
    expect_int("cadtx wait", event_loop_wait(&set, &readfds, 100000), 1);

    ConfigDispatchContext ctx = make_context(slots, &ctrl);
    config_dispatch_context(&ctx, 2, &readfds, buf);

    expect_int("cadtx apply not called",  g_apply_state.calls, 0);
    expect_int("cadtx stored", ctrl.cad_send_after_timeout.load() ? 1 : 0, 1);
    expect_int("cadtx callback not restored",  fake(&ctrl)->callback_count, 0);
    expect_int("cadtx startReceive not called", fake(&ctrl)->start_receive_count, 0);
    expect_int("cadtx client still open", client_slot_has_client(&slots[0]), 1);

    event_loop_close(&set);
    close(sv[0]);
    client_slot_close(&slots[0]);
}

static void test_dispatch_set_cadwait_invalid_rejected(void)
{
    int sv[2];
    ClientSlot slots[2];
    uint8_t buf[buf_SIZE];
    EventLoopSet set;
    EventLoopReadySet readfds;
    RadioController ctrl;
    init_fake_controller(&ctrl, RADIO_HEALTH_FAILED);

    memset(&g_apply_state, 0, sizeof(g_apply_state));
    client_slot_init_all(slots, 2);
    memset(buf, 0, sizeof(buf));

    if (!make_socket_pair(sv)) { g_fail++; return; }
    client_slot_set_fd(&slots[0], sv[1]);
    if (event_loop_init(&set) != 0) {
        close(sv[0]); client_slot_close(&slots[0]); g_fail++;
        printf("[FAIL] cadwait-invalid init\n"); return;
    }

    const char *cmd = "SET CADWAIT=1\n";
    SEND(write(sv[0], cmd, strlen(cmd)));
    event_loop_reset(&set);
    event_loop_add_fd(&set, sv[1]);
    expect_int("cadwait-invalid wait", event_loop_wait(&set, &readfds, 100000), 1);

    ConfigDispatchContext ctx = make_context(slots, &ctrl);
    config_dispatch_context(&ctx, 2, &readfds, buf);

    expect_int("cadwait-invalid apply not called",  g_apply_state.calls, 0);
    expect_int("cadwait-invalid value unchanged",
               (int)ctrl.cad_wait_timeout_ms.load(),
               (int)DAEMON_TX_POLICY_CAD_WAIT_TIMEOUT_MS);
    expect_int("cadwait-invalid client still open", client_slot_has_client(&slots[0]), 1);

    event_loop_close(&set);
    close(sv[0]);
    client_slot_close(&slots[0]);
}

static void test_cad_policy_value_bounds(void)
{
    uint32_t ms = 0;
    int v = 0;

    /* CADWAIT 50..5000 ms */
    expect_int("cadwait lo accept",
               config_status_is_set_cadwait("SET CADWAIT=50", &ms), 1);
    expect_int("cadwait lo value", (int)ms, 50);
    expect_int("cadwait hi accept",
               config_status_is_set_cadwait("SET CADWAIT=5000", &ms), 1);
    expect_int("cadwait hi value", (int)ms, 5000);
    expect_int("cadwait below reject",
               config_status_is_set_cadwait("SET CADWAIT=49", &ms), 0);
    expect_int("cadwait above reject",
               config_status_is_set_cadwait("SET CADWAIT=5001", &ms), 0);
    expect_int("cadwait empty reject",
               config_status_is_set_cadwait("SET CADWAIT=", &ms), 0);

    ms = 123u;
    expect_int("cadwait blank reject",
               config_status_is_set_cadwait("", &ms), 0);
    expect_int("cadwait blank unchanged", (int)ms, 123);
    expect_int("cadwait null reject",
               config_status_is_set_cadwait(NULL, &ms), 0);
    expect_int("cadwait null unchanged", (int)ms, 123);

    expect_int("cadwait suffix reject",
               config_status_is_set_cadwait("SET CADWAIT=300x", &ms), 0);

    /* CADIDLE 0..2000 ms */
    expect_int("cadidle lo accept",
               config_status_is_set_cadidle("SET CADIDLE=0", &ms), 1);
    expect_int("cadidle lo value", (int)ms, 0);
    expect_int("cadidle hi accept",
               config_status_is_set_cadidle("SET CADIDLE=2000", &ms), 1);
    expect_int("cadidle above reject",
               config_status_is_set_cadidle("SET CADIDLE=2001", &ms), 0);

    /* CADPOLL 10..500 ms */
    expect_int("cadpoll lo accept",
               config_status_is_set_cadpoll("SET CADPOLL=10", &ms), 1);
    expect_int("cadpoll hi accept",
               config_status_is_set_cadpoll("SET CADPOLL=500", &ms), 1);
    expect_int("cadpoll below reject",
               config_status_is_set_cadpoll("SET CADPOLL=9", &ms), 0);
    expect_int("cadpoll above reject",
               config_status_is_set_cadpoll("SET CADPOLL=501", &ms), 0);

    /* CADTXAFTERTIMEOUT 0|1 */
    expect_int("cadtx 1 accept",
               config_status_is_set_cadtxaftertimeout("SET CADTXAFTERTIMEOUT=1", &v), 1);
    expect_int("cadtx 1 value", v, 1);
    expect_int("cadtx 0 accept",
               config_status_is_set_cadtxaftertimeout("SET CADTXAFTERTIMEOUT=0", &v), 1);
    expect_int("cadtx 0 value", v, 0);
    expect_int("cadtx 2 reject",
               config_status_is_set_cadtxaftertimeout("SET CADTXAFTERTIMEOUT=2", &v), 0);
}

/* --- CLI parsing and test sequence --- */

/* Audit P1-2: rejected/no-op commands must not re-arm RX (the blind
 * callback+startReceive destroyed a received, undrained packet), and a
 * hardware error mid-apply fails the radio closed. */
static void test_dispatch_rejected_no_rearm(void)
{
    int sv[2];
    ClientSlot slots[2];
    uint8_t buf[buf_SIZE];
    EventLoopSet set;
    EventLoopReadySet readfds;
    RadioController ctrl;

    memset(&g_apply_state, 0, sizeof(g_apply_state));
    g_apply_state.result_override = (int)CONFIG_APPLY_REJECTED_INVALID;
    init_fake_controller(&ctrl, RADIO_HEALTH_READY);
    ctrl.received.store(true);

    client_slot_init_all(slots, 2);
    memset(buf, 0, sizeof(buf));

    if (!make_socket_pair(sv)) {
        g_fail++;
        return;
    }

    client_slot_set_fd(&slots[0], sv[1]);

    if (event_loop_init(&set) != 0) {
        close(sv[0]);
        client_slot_close(&slots[0]);
        g_fail++;
        printf("[FAIL] rejected-no-rearm setup\n");
        return;
    }

    const char *cmd = "SET SF=99\n";
    SEND(write(sv[0], cmd, strlen(cmd)));

    event_loop_reset(&set);
    event_loop_add_fd(&set, sv[1]);
    expect_int("rejected-no-rearm ready wait", event_loop_wait(&set, &readfds, 100000), 1);

    ConfigDispatchContext ctx = make_context(slots, &ctrl);

    config_dispatch_context(&ctx, 2, &readfds, buf);

    expect_int("rejected: apply ran", g_apply_state.calls, 1);
    expect_int("rejected: callback untouched", fake(&ctrl)->callback_count, 0);
    expect_int("rejected: no startReceive",
               fake(&ctrl)->start_receive_count, 0);
    expect_int("rejected: pending RX survives",
               ctrl.received.load() ? 1 : 0, 1);
    expect_int("rejected: health stays READY",
               ctrl.health == RADIO_HEALTH_READY ? 1 : 0, 1);

    g_apply_state.result_override = 0;
    event_loop_close(&set);
    close(sv[0]);
    client_slot_close(&slots[0]);
}

static void test_dispatch_hw_error_fails_closed(void)
{
    int sv[2];
    ClientSlot slots[2];
    uint8_t buf[buf_SIZE];
    EventLoopSet set;
    EventLoopReadySet readfds;
    RadioController ctrl;

    memset(&g_apply_state, 0, sizeof(g_apply_state));
    g_apply_state.result_override = (int)CONFIG_APPLY_HW_ERROR;
    init_fake_controller(&ctrl, RADIO_HEALTH_READY);

    client_slot_init_all(slots, 2);
    memset(buf, 0, sizeof(buf));

    if (!make_socket_pair(sv)) {
        g_fail++;
        return;
    }

    client_slot_set_fd(&slots[0], sv[1]);

    if (event_loop_init(&set) != 0) {
        close(sv[0]);
        client_slot_close(&slots[0]);
        g_fail++;
        printf("[FAIL] hw-error setup\n");
        return;
    }

    const char *cmd = "SET SF=7\n";
    SEND(write(sv[0], cmd, strlen(cmd)));

    event_loop_reset(&set);
    event_loop_add_fd(&set, sv[1]);
    expect_int("hw-error ready wait", event_loop_wait(&set, &readfds, 100000), 1);

    ConfigDispatchContext ctx = make_context(slots, &ctrl);

    config_dispatch_context(&ctx, 2, &readfds, buf);

    expect_int("hw error: apply ran", g_apply_state.calls, 1);
    expect_int("hw error: health FAILED",
               ctrl.health == RADIO_HEALTH_FAILED ? 1 : 0, 1);
    expect_int("hw error: no re-arm", fake(&ctrl)->start_receive_count, 0);

    g_apply_state.result_override = 0;
    event_loop_close(&set);
    close(sv[0]);
    client_slot_close(&slots[0]);
}

/* Audit P1-5: RF CONFIG is rejected while queued TX jobs exist (a job
 * transmits with execution-time configuration; retuning pending jobs is the
 * hazard). Status/GET queries and the drained case pass through. */
static void test_dispatch_defers_config_while_queue_busy(void)
{
    int sv[2];
    ClientSlot slots[2];
    uint8_t buf[buf_SIZE];
    EventLoopSet set;
    EventLoopReadySet readfds;
    RadioController ctrl;

    memset(&g_apply_state, 0, sizeof(g_apply_state));
    init_fake_controller(&ctrl, RADIO_HEALTH_READY);
    g_stub_pending = 2;

    client_slot_init_all(slots, 2);
    memset(buf, 0, sizeof(buf));

    if (!make_socket_pair(sv)) {
        g_fail++;
        return;
    }

    client_slot_set_fd(&slots[0], sv[1]);

    if (event_loop_init(&set) != 0) {
        close(sv[0]);
        client_slot_close(&slots[0]);
        g_fail++;
        printf("[FAIL] queue-busy setup\n");
        return;
    }

    const char *cmd = "SET SF=7\n";
    SEND(write(sv[0], cmd, strlen(cmd)));

    event_loop_reset(&set);
    event_loop_add_fd(&set, sv[1]);
    expect_int("queue-busy ready wait", event_loop_wait(&set, &readfds, 100000), 1);

    ConfigDispatchContext ctx = make_context(slots, &ctrl);

    config_dispatch_context(&ctx, 2, &readfds, buf);
    expect_int("queue busy: apply not called", g_apply_state.calls, 0);
    expect_int("queue busy: no re-arm", fake(&ctrl)->start_receive_count, 0);

    /* Executing-only (pop-to-transmit window) also defers. */
    g_stub_pending = 0;
    g_stub_job_active = 1;
    SEND(write(sv[0], cmd, strlen(cmd)));
    event_loop_reset(&set);
    event_loop_add_fd(&set, sv[1]);
    expect_int("executing ready wait", event_loop_wait(&set, &readfds, 100000), 1);
    config_dispatch_context(&ctx, 2, &readfds, buf);
    expect_int("job executing: apply not called", g_apply_state.calls, 0);

    /* Drained queue: the same command applies. */
    g_stub_job_active = 0;
    SEND(write(sv[0], cmd, strlen(cmd)));
    event_loop_reset(&set);
    event_loop_add_fd(&set, sv[1]);
    expect_int("drained ready wait", event_loop_wait(&set, &readfds, 100000), 1);
    config_dispatch_context(&ctx, 2, &readfds, buf);
    expect_int("queue drained: apply runs", g_apply_state.calls, 1);

    event_loop_close(&set);
    close(sv[0]);
    client_slot_close(&slots[0]);
}

/* --- Stable CONF replies (audit item 6) ---------------------------------- */

static int read_reply(int fd, char *out, size_t out_size)
{
    ssize_t n;

    usleep(20000); /* let the flush land */
    n = read(fd, out, out_size - 1);
    if (n < 0)
        n = 0;
    out[n] = '\0';
    return (int)n;
}

/* One dispatch round: send `cmd`, return the reply text in `out`. */
static void dispatch_round(ClientSlot *slots, RadioController *ctrl,
                           int sv[2], EventLoopSet *set,
                           EventLoopReadySet *readfds, uint8_t *buf,
                           const char *cmd, char *out, size_t out_size)
{
    ConfigDispatchContext ctx = make_context(slots, ctrl);

    SEND(write(sv[0], cmd, strlen(cmd)));
    event_loop_reset(set);
    event_loop_add_fd(set, sv[1]);
    if (event_loop_wait(set, readfds, 100000) != 1) {
        g_fail++;
        printf("[FAIL] dispatch_round wait (%s)\n", cmd);
        out[0] = '\0';
        return;
    }
    config_dispatch_context(&ctx, 2, readfds, buf);
    read_reply(sv[0], out, out_size);
}

static void test_stable_conf_replies(void)
{
    int sv[2];
    ClientSlot slots[2];
    uint8_t buf[buf_SIZE];
    EventLoopSet set;
    EventLoopReadySet readfds;
    RadioController ctrl;
    char out[512];

    memset(&g_apply_state, 0, sizeof(g_apply_state));
    init_fake_controller(&ctrl, RADIO_HEALTH_READY);
    client_slot_init_all(slots, 2);
    memset(buf, 0, sizeof(buf));

    if (!make_socket_pair(sv)) {
        g_fail++;
        return;
    }
    client_slot_set_fd(&slots[0], sv[1]);
    if (event_loop_init(&set) != 0) {
        close(sv[0]);
        client_slot_close(&slots[0]);
        g_fail++;
        return;
    }

    /* Runtime setter: OK, exactly once. */
    dispatch_round(slots, &ctrl, sv, &set, &readfds, buf,
                   "SET TXRESULT=1\n", out, sizeof(out));
    expect_str("reply: runtime setter OK", out, "OK\n");

    /* Applied CONFIG: OK. */
    g_apply_state.result_override = 0; /* APPLIED */
    dispatch_round(slots, &ctrl, sv, &set, &readfds, buf,
                   "SET SF=7\n", out, sizeof(out));
    expect_str("reply: applied OK", out, "OK\n");

    /* Policy rejection: ERR INVALID. */
    g_apply_state.result_override = (int)CONFIG_APPLY_REJECTED_INVALID;
    dispatch_round(slots, &ctrl, sv, &set, &readfds, buf,
                   "SET SF=99\n", out, sizeof(out));
    expect_str("reply: invalid", out, "ERR INVALID\n");

    /* Malformed: ERR MALFORMED. */
    g_apply_state.result_override = (int)CONFIG_APPLY_REJECTED_MALFORMED;
    dispatch_round(slots, &ctrl, sv, &set, &readfds, buf,
                   "SET SF\n", out, sizeof(out));
    expect_str("reply: malformed", out, "ERR MALFORMED\n");

    /* Unknown: ERR UNKNOWN. */
    g_apply_state.result_override = (int)CONFIG_APPLY_UNKNOWN;
    dispatch_round(slots, &ctrl, sv, &set, &readfds, buf,
                   "BOGUS\n", out, sizeof(out));
    expect_str("reply: unknown", out, "ERR UNKNOWN\n");

    /* Hardware failure: ERR HARDWARE (+ FAILED transition elsewhere). */
    g_apply_state.result_override = (int)CONFIG_APPLY_HW_ERROR;
    dispatch_round(slots, &ctrl, sv, &set, &readfds, buf,
                   "SET SF=7\n", out, sizeof(out));
    expect_str("reply: hardware", out, "ERR HARDWARE\n");
    g_apply_state.result_override = 0;

    /* Radio not ready: ERR RADIO_NOT_READY. */
    ctrl.health = RADIO_HEALTH_FAILED;
    dispatch_round(slots, &ctrl, sv, &set, &readfds, buf,
                   "SET SF=7\n", out, sizeof(out));
    expect_str("reply: not ready", out, "ERR RADIO_NOT_READY\n");
    ctrl.health = RADIO_HEALTH_READY;

    /* GET STATUS: one data line, NO trailing OK. */
    dispatch_round(slots, &ctrl, sv, &set, &readfds, buf,
                   "GET STATUS\n", out, sizeof(out));
    expect_int("reply: GET STATUS is data line",
               strncmp(out, "STATUS ", 7) == 0, 1);
    expect_int("reply: GET STATUS no trailing OK",
               strstr(out, "OK\n") == NULL, 1);

    event_loop_close(&set);
    close(sv[0]);
    client_slot_close(&slots[0]);
}

/* Audit item 1: TXQUEUE=0 drain-before-disable transition contract. */
static void test_txqueue_disable_transition(void)
{
    int sv[2];
    ClientSlot slots[2];
    uint8_t buf[buf_SIZE];
    EventLoopSet set;
    EventLoopReadySet readfds;
    RadioController ctrl;
    char out[128];

    memset(&g_apply_state, 0, sizeof(g_apply_state));
    init_fake_controller(&ctrl, RADIO_HEALTH_READY);
    client_slot_init_all(slots, 2);
    memset(buf, 0, sizeof(buf));

    if (!make_socket_pair(sv)) {
        g_fail++;
        return;
    }
    client_slot_set_fd(&slots[0], sv[1]);
    if (event_loop_init(&set) != 0) {
        close(sv[0]);
        client_slot_close(&slots[0]);
        g_fail++;
        return;
    }

    /* Pending jobs: disable rejected, flag unchanged, no radio touch. */
    g_stub_pending = 3;
    dispatch_round(slots, &ctrl, sv, &set, &readfds, buf,
                   "SET TXQUEUE=0\n", out, sizeof(out));
    expect_str("txqueue: pending rejects disable", out, "ERR BUSY\n");
    expect_int("txqueue: flag stays enabled",
               ctrl.tx_queue_active.load() ? 1 : 0, 1);
    expect_int("txqueue: no apply on rejection", g_apply_state.calls, 0);
    expect_int("txqueue: no re-arm on rejection",
               fake(&ctrl)->start_receive_count, 0);

    /* Executing-only job: still rejected. */
    g_stub_pending = 0;
    g_stub_job_active = 1;
    dispatch_round(slots, &ctrl, sv, &set, &readfds, buf,
                   "SET TXQUEUE=0\n", out, sizeof(out));
    expect_str("txqueue: executing rejects disable", out, "ERR BUSY\n");
    expect_int("txqueue: flag still enabled",
               ctrl.tx_queue_active.load() ? 1 : 0, 1);

    /* Drained: disable succeeds; enable stays trivial. */
    g_stub_job_active = 0;
    dispatch_round(slots, &ctrl, sv, &set, &readfds, buf,
                   "SET TXQUEUE=0\n", out, sizeof(out));
    expect_str("txqueue: drained disable OK", out, "OK\n");
    expect_int("txqueue: flag disabled",
               ctrl.tx_queue_active.load() ? 1 : 0, 0);

    dispatch_round(slots, &ctrl, sv, &set, &readfds, buf,
                   "SET TXQUEUE=1\n", out, sizeof(out));
    expect_str("txqueue: enable OK", out, "OK\n");
    expect_int("txqueue: flag re-enabled",
               ctrl.tx_queue_active.load() ? 1 : 0, 1);

    event_loop_close(&set);
    close(sv[0]);
    client_slot_close(&slots[0]);
}

/* Audit P2: invalid reserved runtime setters answer ERR INVALID (or
 * ERR MALFORMED when incomplete), classified from the REAL input strings and
 * independent of radio readiness. */
static void test_reserved_setter_classification(void)
{
    int sv[2];
    ClientSlot slots[2];
    uint8_t buf[buf_SIZE];
    EventLoopSet set;
    EventLoopReadySet readfds;
    RadioController ctrl;
    char out[128];

    memset(&g_apply_state, 0, sizeof(g_apply_state));
    init_fake_controller(&ctrl, RADIO_HEALTH_READY);
    client_slot_init_all(slots, 2);
    memset(buf, 0, sizeof(buf));

    if (!make_socket_pair(sv)) {
        g_fail++;
        return;
    }
    client_slot_set_fd(&slots[0], sv[1]);
    if (event_loop_init(&set) != 0) {
        close(sv[0]);
        client_slot_close(&slots[0]);
        g_fail++;
        return;
    }

    /* Out-of-range / invalid values -> ERR INVALID, generic path untouched. */
    dispatch_round(slots, &ctrl, sv, &set, &readfds, buf,
                   "SET CADWAIT=1\n", out, sizeof(out));
    expect_str("setter: CADWAIT=1 invalid", out, "ERR INVALID\n");
    dispatch_round(slots, &ctrl, sv, &set, &readfds, buf,
                   "SET TXQUEUE=2\n", out, sizeof(out));
    expect_str("setter: TXQUEUE=2 invalid", out, "ERR INVALID\n");
    dispatch_round(slots, &ctrl, sv, &set, &readfds, buf,
                   "SET TXMODE=foo\n", out, sizeof(out));
    expect_str("setter: TXMODE=foo invalid", out, "ERR INVALID\n");
    dispatch_round(slots, &ctrl, sv, &set, &readfds, buf,
                   "SET TXRESULT=9\n", out, sizeof(out));
    expect_str("setter: TXRESULT=9 invalid", out, "ERR INVALID\n");
    expect_int("setter: generic apply never ran", g_apply_state.calls, 0);

    /* Structurally incomplete -> ERR MALFORMED. */
    dispatch_round(slots, &ctrl, sv, &set, &readfds, buf,
                   "SET CADWAIT\n", out, sizeof(out));
    expect_str("setter: bare CADWAIT malformed", out, "ERR MALFORMED\n");
    dispatch_round(slots, &ctrl, sv, &set, &readfds, buf,
                   "SET TXQUEUE=\n", out, sizeof(out));
    expect_str("setter: empty TXQUEUE malformed", out, "ERR MALFORMED\n");

    /* Independent of radio readiness. */
    ctrl.health = RADIO_HEALTH_FAILED;
    dispatch_round(slots, &ctrl, sv, &set, &readfds, buf,
                   "SET CADWAIT=1\n", out, sizeof(out));
    expect_str("setter: invalid beats NOT_READY", out, "ERR INVALID\n");
    ctrl.health = RADIO_HEALTH_READY;

    /* Valid reserved setter still answers OK (real parser, no override). */
    dispatch_round(slots, &ctrl, sv, &set, &readfds, buf,
                   "SET CADWAIT=1500\n", out, sizeof(out));
    expect_str("setter: valid CADWAIT OK", out, "OK\n");

    /* Case-insensitive like every documented key: lowercase valid setters
     * work, lowercase invalid ones classify as INVALID (not UNKNOWN). */
    dispatch_round(slots, &ctrl, sv, &set, &readfds, buf,
                   "SET txqueue=1\n", out, sizeof(out));
    expect_str("setter: lowercase valid OK", out, "OK\n");
    expect_int("setter: lowercase valid applied",
               ctrl.tx_queue_active.load() ? 1 : 0, 1);
    dispatch_round(slots, &ctrl, sv, &set, &readfds, buf,
                   "SET txqueue=2\n", out, sizeof(out));
    expect_str("setter: lowercase invalid classified", out, "ERR INVALID\n");
    dispatch_round(slots, &ctrl, sv, &set, &readfds, buf,
                   "SET TxMode=direct\n", out, sizeof(out));
    expect_str("setter: mixed-case TXMODE OK", out, "OK\n");
    dispatch_round(slots, &ctrl, sv, &set, &readfds, buf,
                   "set cadwait=1\n", out, sizeof(out));
    expect_str("setter: lowercase command word classified",
               out, "ERR INVALID\n");

    event_loop_close(&set);
    close(sv[0]);
    client_slot_close(&slots[0]);
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

    test_dispatch_ready_client();
    test_dispatch_rejected_no_rearm();
    test_dispatch_hw_error_fails_closed();
    test_dispatch_defers_config_while_queue_busy();
    test_stable_conf_replies();
    test_txqueue_disable_transition();
    test_reserved_setter_classification();
    test_dispatch_ready_client_epoll();
    test_dispatch_ignores_not_ready_client();
    test_dispatch_sets_txmode_without_radio();
    test_get_channel_pending_rx_guard();
    test_get_channel_noscan_never_scans();
    test_get_channel_noscan_null_controller();
    test_dispatch_get_channel_noscan_touches_nothing();
    test_legacy_channel_pending_survives_a_drain();
    test_noscan_never_blocks_on_a_transmit();
    test_get_channel_command_matching_is_exact();
    test_set_cadrssi_parser();
    test_dispatch_sets_cadmonitor_optin();
    test_status_uses_cad_broadcast_latch();
    test_status_reports_high_power_and_family();
    test_dispatch_get_channel_restores_rx();
    test_dispatch_get_channel_during_tx_skips_scan();
    test_dispatch_set_txresult();
    test_dispatch_set_txqueue();
    test_dispatch_closes_eof_client();
    test_dispatch_set_cadwait();
    test_dispatch_set_cadidle();
    test_dispatch_set_cadpoll();
    test_dispatch_set_cadtxaftertimeout();
    test_dispatch_set_cadwait_invalid_rejected();
    test_cad_policy_value_bounds();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
