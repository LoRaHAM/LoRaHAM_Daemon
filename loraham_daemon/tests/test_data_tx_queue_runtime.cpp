#include "../daemon_band.h"
#include "../daemon_data_tx_runtime.h"

#include <chrono>
#include <stdio.h>
#include <thread>
#include <string.h>

/* --- DATA TX queue opt-in runtime tests --------------------------------- */

static int g_ok = 0;
static int g_fail = 0;
static int g_led_set_count = 0;
static int g_led_last_state = -1;


/* --- Local production-symbol stubs for this unit test --- */
TxResult lora_send(uint8_t *buf, size_t len, int band)
{
    (void)band;
    (void)buf;
    (void)len;
    return TX_RESULT_RADIO_ERROR;
}

int daemon_led_init(void)
{
    return 0;
}

int daemon_led_ready(void)
{
    return 1;
}

void daemon_led_set_pin(int pin, int state)
{
    (void)pin;
    g_led_set_count++;
    g_led_last_state = state;
}

void daemon_led_flash_pin(int pin)
{
    (void)pin;
}
/* --- End local production-symbol stubs --- */





// Fake driver: overrides the virtual RadioDriver delegates and counts calls
// exactly as the earlier template fake did (counter semantics unchanged).
struct FakeRadio : public RadioDriver {
    int scan_count;
    int scan_state;
    int scan_pattern[16];
    int scan_pattern_len;
    int callback_count;
    int clear_callback_count;
    int start_receive_count;
    float rssi;
    void (*last_callback)(void);

    FakeRadio() : RadioDriver(NULL),
                  scan_count(0), scan_state(0), scan_pattern_len(0),
                  callback_count(0), clear_callback_count(0), start_receive_count(0), rssi(-81.0f),
                  last_callback(NULL)
    {
        memset(scan_pattern, 0, sizeof(scan_pattern));
    }

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
        rx_packet_pending = false;
        return 0;
    }

    int16_t scanChannel() override
    {
        int idx = scan_count;

        scan_count++;
        if (idx < scan_pattern_len)
            return scan_pattern[idx];

        return scan_state;
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
    bool rx_completes_on_standby = false;
    bool rx_packet_pending = false;

    int16_t standby() override
    {
        if (standby_count++ == 0 && rx_completes_on_standby)
            rx_packet_pending = true;   /* a packet landed as RX was stopped */
        return 0;
    }

    bool rxDonePending() override { return rx_packet_pending; }



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

typedef struct {
    int calls;
    size_t len[8];
    int band[8];
    TxResult result[8];
} FakeSender;

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

static TxResult fake_send(uint8_t *payload, size_t len, int band, void *ctx)
{
    FakeSender *sender = (FakeSender *)ctx;
    int idx = sender->calls;

    (void)payload;

    sender->len[idx] = len;
    sender->band[idx] = band;
    sender->calls++;

    return sender->result[idx];
}


static int wait_async_processed(size_t expected)
{
    for (int i = 0; i < 1000; i++) {
        if (daemon_tx_async_runtime_processed() >= expected)
            return 1;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return 0;
}

static void init_context(RadioController *ctrl,
                         DataTxDaemonContext *ctx,
                         FakeSender *sender)
{
    radio_controller_init(ctrl,
                          RADIO_BAND_433,
                          "TEST",
                          false,
                          fake_rx_callback,
                          13);
    ctrl->driver.reset(new FakeRadio());
    ctrl->health = RADIO_HEALTH_READY;
    ctrl->mode = RADIO_MODE_FSK;
    ctrl->cad_wait_timeout_ms.store(2u);
    ctrl->cad_idle_stable_ms.store(1u);
    ctrl->cad_poll_interval_ms.store(1u);
    ctrl->cad_send_after_timeout.store(false);

    daemon_tx_async_runtime_shutdown();
    daemon_tx_async_runtime_init();

    memset(sender, 0, sizeof(*sender));
    sender->result[0] = TX_RESULT_OK;

    ctx->ctrl = ctrl;
    ctx->log_ctx = "TEST";
    ctx->send_fn = fake_send;
    ctx->send_ctx = sender;
    ctx->completion_slot = DAEMON_TX_COMPLETION_SLOT_NONE;
    ctx->completion_seq = 0;
    ctx->completion_generation = 0u;
    ctx->tx_busy_wait_ticks = 2;
    ctx->tx_busy_sleep_usec = 0;
    ctx->cad_wait_ticks = 2;
    ctx->cad_idle_stable_ticks = 1;
    ctx->cad_sleep_usec = 0;
}

static void test_default_direct_path(void)
{
    RadioController ctrl;
    DataTxDaemonContext ctx;
    FakeSender sender;
    uint8_t payload[] = { 1, 2, 3 };

    init_context(&ctrl, &ctx, &sender);
    expect_int("default txqueue enabled", ctrl.tx_queue_active.load() ? 1 : 0, 1);
    ctrl.tx_queue_active.store(false);

    expect_int("direct tx result",
               send_data_chunk(payload, sizeof(payload), 0, &ctx),
               0);
    expect_int("direct sender calls", sender.calls, 1);
    expect_int("direct frontdoor led untouched", g_led_set_count, 0);
    expect_size("direct async accepted",
                daemon_tx_async_runtime_accepted(), 0);
    expect_size("direct async processed",
                daemon_tx_async_runtime_processed(), 0);
    expect_size("direct async pending",
                daemon_tx_async_runtime_pending(), 0);
}

/*
 * HW-5, at the consumer that matters. init_context() puts this controller in
 * SX127x FSK, where the FIFO allows 63 payload bytes.
 *
 * The raw DATA path chunks to the limit and never arrives oversized, but a
 * framed client sends its own length: a 64-byte TX_PACKET parses correctly
 * into the 255-byte frame buffer, and before this repair only the radio's FIFO
 * said it was too big. It must be refused BEFORE any queue or radio mutation
 * -- so the assertions are not only on the return value but on the sender
 * never being called and the queue staying empty.
 */
static void test_fsk_payload_boundary(void)
{
    RadioController ctrl;
    DataTxDaemonContext ctx;
    FakeSender sender;
    uint8_t payload[256];

    memset(payload, 0x5A, sizeof(payload));

    init_context(&ctrl, &ctx, &sender);
    ctrl.tx_queue_active.store(false);

    expect_int("fsk 63 bytes is sent", send_data_chunk(payload, 63, 0, &ctx),
               DAEMON_TX_OUTCOME_OK);
    expect_int("fsk 63 reached the sender", sender.calls, 1);

    expect_int("fsk 64 bytes is rejected as invalid",
               send_data_chunk(payload, 64, 0, &ctx),
               DAEMON_TX_OUTCOME_INVALID_PACKET);
    expect_int("fsk 64 never reached the sender", sender.calls, 1);

    expect_int("fsk 255 bytes is rejected as invalid",
               send_data_chunk(payload, 255, 0, &ctx),
               DAEMON_TX_OUTCOME_INVALID_PACKET);
    expect_int("fsk 255 never reached the sender", sender.calls, 1);

    /* And nothing was queued on the way to being rejected. */
    expect_size("rejected frames queued nothing",
                daemon_tx_async_runtime_accepted(), 0);

    /* 62 for the lower boundary: the limit is a ceiling, not a fixed size. */
    expect_int("fsk 62 bytes is sent", send_data_chunk(payload, 62, 0, &ctx),
               DAEMON_TX_OUTCOME_OK);
    expect_int("fsk 62 reached the sender", sender.calls, 2);
}

/* The other half of the same rule: LoRa must not lose a single byte to it. */
static void test_lora_keeps_the_full_payload(void)
{
    RadioController ctrl;
    DataTxDaemonContext ctx;
    FakeSender sender;
    uint8_t payload[256];

    memset(payload, 0x5A, sizeof(payload));

    init_context(&ctrl, &ctx, &sender);
    ctrl.tx_queue_active.store(false);
    ctrl.mode = RADIO_MODE_LORA;
    ctrl.tx_mode = RADIO_TX_MODE_DIRECT;   /* no CAD wait in this test */

    expect_int("lora 255 bytes is sent", send_data_chunk(payload, 255, 0, &ctx),
               DAEMON_TX_OUTCOME_OK);
    expect_int("lora 255 reached the sender", sender.calls, 1);

    expect_int("lora 64 bytes is sent", send_data_chunk(payload, 64, 0, &ctx),
               DAEMON_TX_OUTCOME_OK);
    expect_int("lora 64 reached the sender", sender.calls, 2);
}

/* Audit item 1 (defense in depth): with the queue disabled but residual
 * async work present, the direct path rejects BUSY instead of waiting
 * behind or interleaving with queued CAD+TX. */
static void test_direct_refuses_residual_async_work(void)
{
    RadioController ctrl;
    DataTxDaemonContext ctx;
    FakeSender sender;
    uint8_t payload[] = { 1, 2, 3 };

    init_context(&ctrl, &ctx, &sender);
    ctrl.tx_queue_active.store(false);

    /* Residual executing job (pop-to-transmit window). */
    daemon_tx_async_runtime_worker()->job_active.store(true);

    expect_int("residual-active direct tx rejected BUSY",
               send_data_chunk(payload, sizeof(payload), 0, &ctx),
               DAEMON_TX_OUTCOME_BUSY);
    expect_int("residual-active sender NOT called", sender.calls, 0);

    daemon_tx_async_runtime_worker()->job_active.store(false);

    /* Residual pending job. */
    DaemonTxJob job;
    daemon_tx_job_init(&job, 433, RADIO_TX_MODE_MANAGED, 1);
    daemon_tx_job_set_payload(&job, payload, sizeof(payload));
    daemon_tx_queue_push(&daemon_tx_async_runtime_worker()->worker.queue,
                         &job);

    expect_int("residual-pending direct tx rejected BUSY",
               send_data_chunk(payload, sizeof(payload), 0, &ctx),
               DAEMON_TX_OUTCOME_BUSY);
    expect_int("residual-pending sender NOT called", sender.calls, 0);

    /* Drain the residue; the direct path works again. */
    daemon_tx_queue_pop(&daemon_tx_async_runtime_worker()->worker.queue,
                        &job);
    expect_int("drained direct tx result",
               send_data_chunk(payload, sizeof(payload), 0, &ctx), 0);
    expect_int("drained sender called", sender.calls, 1);
}

static void test_txqueue_optin_path(void)
{
    RadioController ctrl;
    DataTxDaemonContext ctx;
    FakeSender sender;
    uint8_t payload[] = { 4, 5, 6, 7 };

    init_context(&ctrl, &ctx, &sender);
    ctrl.tx_queue_active.store(true);
    ctx.completion_slot = 3;
    ctx.completion_seq = 44;
    ctx.completion_generation = 9u;

    expect_int("queue tx result",
               send_data_chunk(payload, sizeof(payload), 0, &ctx),
               0);
    expect_int("queue processed wait", wait_async_processed(1), 1);
    expect_int("queue sender calls", sender.calls, 1);
    expect_int("queue frontdoor led untouched", g_led_set_count, 0);
    expect_size("queue stats tx ok from completion", ctrl.stats.tx_ok, 1);
    expect_size("queue stats tx busy", ctrl.stats.tx_busy, 0);
    expect_size("queue async accepted", daemon_tx_async_runtime_accepted(), 1);
    expect_size("queue async processed", daemon_tx_async_runtime_processed(), 1);
    expect_size("queue async pending drained", daemon_tx_async_runtime_pending(), 0);

    DaemonTxJobResult last;
    expect_int("queue async last present",
               daemon_tx_async_runtime_last_result(&last),
               1);
    expect_int("queue async last result", last.tx_result, TX_RESULT_OK);

    expect_size("queue completion pending",
                daemon_tx_async_runtime_completion_pending(),
                1);
    DaemonTxJobResult completion;
    expect_int("queue completion pop",
               daemon_tx_async_runtime_pop_completion(&completion),
               0);
    expect_int("queue completion result", completion.tx_result, TX_RESULT_OK);
    expect_int("queue completion target", completion.completion_slot, 3);
    expect_int("queue completion generation", completion.completion_generation, 9u);
    expect_int("queue completion seq", completion.seq, 44);
    expect_size("queue completion pending drained",
                daemon_tx_async_runtime_completion_pending(),
                0);
}

static void test_txqueue_direct_full_rejects_newest(void)
{
    DaemonTxAsyncWorker *worker;

    daemon_tx_async_runtime_shutdown();
    daemon_tx_async_runtime_init();

    worker = daemon_tx_async_runtime_worker();

    for (int i = 0; i < DAEMON_TX_QUEUE_CAPACITY; i++) {
        DaemonTxJob job;
        uint8_t queued[] = { (uint8_t)i };

        daemon_tx_job_init(&job, 433, RADIO_TX_MODE_MANAGED, (uint16_t)i);
        daemon_tx_job_set_payload(&job, queued, sizeof(queued));
        daemon_tx_async_worker_submit(worker, &job);
    }

    DaemonTxJob extra;
    uint8_t payload[] = { 8, 9 };
    daemon_tx_job_init(&extra, 433, RADIO_TX_MODE_MANAGED, 99);
    daemon_tx_job_set_payload(&extra, payload, sizeof(payload));

    expect_int("queue full direct reject",
               daemon_tx_async_worker_submit(worker, &extra),
               -1);
    expect_size("queue full rejected", daemon_tx_async_runtime_rejected(), 1);
    expect_size("queue full dropped", daemon_tx_async_runtime_dropped(), 0);
    expect_size("queue full still pending", daemon_tx_async_runtime_pending(),
                DAEMON_TX_QUEUE_CAPACITY);

    daemon_tx_async_runtime_shutdown();
}


static void test_direct_tx_busy_timeout_blocks_send(void)
{
    RadioController ctrl;
    DataTxDaemonContext ctx;
    FakeSender sender;
    uint8_t payload[] = { 1, 2 };

    init_context(&ctrl, &ctx, &sender);
    ctrl.tx_queue_active.store(false);
    ctrl.tx_busy.store(true);

    expect_int("direct tx busy wait times out",
               data_tx_wait_tx_ready_with_limits(&ctrl, 2, 0),
               1);
    expect_int("direct tx busy no send",
               send_data_chunk(payload, sizeof(payload), 0, &ctx),
               DAEMON_TX_OUTCOME_CHANNEL_BUSY);
    expect_int("direct tx busy sender calls", sender.calls, 0);
    expect_size("direct tx busy stats", ctrl.stats.tx_busy, 1);
}

static void test_queued_tx_skips_frontdoor_tx_busy_wait(void)
{
    RadioController ctrl;
    DataTxDaemonContext ctx;
    FakeSender sender;
    uint8_t payload[] = { 2, 3 };

    init_context(&ctrl, &ctx, &sender);
    ctrl.tx_queue_active.store(true);
    ctrl.tx_busy.store(true);

    expect_int("queued tx accepted while tx busy",
               send_data_chunk(payload, sizeof(payload), 0, &ctx),
               0);
    expect_int("queued tx processed wait", wait_async_processed(1), 1);
    expect_int("queued tx sender calls", sender.calls, 1);
    expect_size("queued tx busy frontdoor stats", ctrl.stats.tx_busy, 0);
}

static void test_tx_busy_wait_clears(void)
{
    RadioController ctrl;
    DataTxDaemonContext ctx;
    FakeSender sender;

    init_context(&ctrl, &ctx, &sender);
    ctrl.tx_busy.store(false);

    expect_int("tx busy clear immediately",
               data_tx_wait_tx_ready_with_limits(&ctrl, 2, 0),
               0);
}


static void test_direct_transmits_on_busy_channel_sync(void)
{
    RadioController ctrl;
    DataTxDaemonContext ctx;
    FakeSender sender;
    uint8_t payload[] = { 9 };

    init_context(&ctrl, &ctx, &sender);
    ctrl.tx_queue_active.store(false);
    ctrl.mode = RADIO_MODE_LORA;
    ctrl.tx_mode = RADIO_TX_MODE_DIRECT;
    fake(&ctrl)->scan_state = 1; // channel busy

    // DIRECT transmits immediately even on a busy LoRa channel: no CAD probe,
    // no busy/timeout drop.
    expect_int("direct busy tx result",
               send_data_chunk(payload, sizeof(payload), 0, &ctx),
               0);
    expect_int("direct busy sends", sender.calls, 1);
    expect_int("direct busy no cad probe", fake(&ctrl)->scan_count, 0);
}

static void test_managed_busy_timeout_policy_blocks_by_default(void)
{
    RadioController ctrl;
    DataTxDaemonContext ctx;
    FakeSender sender;
    DaemonTxJob job;

    init_context(&ctrl, &ctx, &sender);
    ctrl.mode = RADIO_MODE_LORA;
    ctrl.tx_mode = RADIO_TX_MODE_MANAGED;
    fake(&ctrl)->scan_state = 1;

    expect_int("managed busy timeout blocks by default",
               data_tx_wait_channel_free_with_limits(&ctx, 2, 3, 0),
               DATA_TX_CAD_WAIT_BLOCK);
    expect_int("managed busy timeout probe count", fake(&ctrl)->scan_count, 2);

    daemon_tx_job_init(&job, 433, RADIO_TX_MODE_MANAGED, 79);
    data_tx_configure_job_cad_policy(&ctx, &job);
    expect_int("managed queued timeout blocks by default",
               job.cad_send_after_timeout,
               0);
}

static void test_managed_free_waits_for_stable_idle(void)
{
    RadioController ctrl;
    DataTxDaemonContext ctx;
    FakeSender sender;

    init_context(&ctrl, &ctx, &sender);
    ctrl.mode = RADIO_MODE_LORA;
    ctrl.tx_mode = RADIO_TX_MODE_MANAGED;
    fake(&ctrl)->scan_state = 0;

    expect_int("managed stable idle sends",
               data_tx_wait_channel_free_with_limits(&ctx, 5, 3, 0),
               DATA_TX_CAD_WAIT_FREE);
    expect_int("managed stable idle probes", fake(&ctrl)->scan_count, 3);
}

static void test_managed_busy_resets_stable_idle(void)
{
    RadioController ctrl;
    DataTxDaemonContext ctx;
    FakeSender sender;

    init_context(&ctrl, &ctx, &sender);
    ctrl.mode = RADIO_MODE_LORA;
    ctrl.tx_mode = RADIO_TX_MODE_MANAGED;
    fake(&ctrl)->scan_pattern[0] = 0;
    fake(&ctrl)->scan_pattern[1] = 1;
    fake(&ctrl)->scan_pattern[2] = 0;
    fake(&ctrl)->scan_pattern[3] = 0;
    fake(&ctrl)->scan_pattern[4] = 0;
    fake(&ctrl)->scan_pattern_len = 5;

    expect_int("managed busy resets idle sends",
               data_tx_wait_channel_free_with_limits(&ctx, 5, 3, 0),
               DATA_TX_CAD_WAIT_FREE);
    expect_int("managed busy resets idle probes", fake(&ctrl)->scan_count, 5);
}



static void test_cad_timeout_sets_result_flag(void)
{
    DaemonTxJob job;

    daemon_tx_job_init(&job, 433, RADIO_TX_MODE_MANAGED, 77);
    job.flags = FRAMED_DATA_TX_RESULT_FLAG_MANAGED;

    data_tx_apply_cad_decision_flags(&job, DATA_TX_CAD_WAIT_TIMEOUT_SEND);
    expect_int("cad timeout flag set",
               job.flags & FRAMED_DATA_TX_RESULT_FLAG_CAD_TIMEOUT,
               FRAMED_DATA_TX_RESULT_FLAG_CAD_TIMEOUT);
}

static void test_cad_free_does_not_set_timeout_flag(void)
{
    DaemonTxJob job;

    daemon_tx_job_init(&job, 433, RADIO_TX_MODE_MANAGED, 78);
    job.flags = FRAMED_DATA_TX_RESULT_FLAG_MANAGED;

    data_tx_apply_cad_decision_flags(&job, DATA_TX_CAD_WAIT_FREE);
    expect_int("cad free timeout flag clear",
               job.flags & FRAMED_DATA_TX_RESULT_FLAG_CAD_TIMEOUT,
               0);
}

static void test_worker_cad_mode_check_uses_radio_lock(void)
{
    RadioController ctrl;
    DataTxDaemonContext ctx;
    FakeSender sender;

    init_context(&ctrl, &ctx, &sender);
    ctrl.mode = RADIO_MODE_FSK;

    expect_int("worker FSK CAD reports free",
               daemon_data_tx_worker_cad_probe(433, &ctrl),
               DAEMON_TX_CAD_PROBE_FREE);
    expect_int("worker FSK CAD does not scan",
               fake(&ctrl)->scan_count,
               0);

    daemon_tx_async_runtime_shutdown();
}


static void test_queued_cad_callback_survives_later_non_cad_config(void)
{
    RadioController ctrl;
    DataTxDaemonContext ctx;
    FakeSender sender;
    DaemonTxAsyncWorker *worker;
    DaemonTxJob protected_job;
    DaemonTxJob later_non_cad_job;
    DaemonTxJobResult last;
    uint8_t payload[] = { 0x42 };

    init_context(&ctrl, &ctx, &sender);
    ctrl.mode = RADIO_MODE_LORA;
    ctrl.tx_mode = RADIO_TX_MODE_MANAGED;
    fake(&ctrl)->scan_state = 1;

    worker = daemon_tx_async_runtime_worker();
    daemon_tx_async_worker_configure(worker, fake_send, &sender, NULL, NULL);

    daemon_tx_job_init(&protected_job, 433, RADIO_TX_MODE_MANAGED, 90);
    daemon_tx_job_configure_cad_policy(&protected_job, 1, 1, 1, 0, 0);
    daemon_tx_job_set_payload(&protected_job, payload, sizeof(payload));
    daemon_data_tx_configure_worker_cad(worker, &ctx, &protected_job);
    expect_int("queued CAD callback queue protected",
               daemon_tx_async_worker_submit(worker, &protected_job),
               0);

    daemon_tx_job_init(&later_non_cad_job, 433, RADIO_TX_MODE_MANAGED, 91);
    daemon_data_tx_configure_worker_cad(worker, &ctx, &later_non_cad_job);

    expect_int("queued CAD callback start",
               daemon_tx_async_worker_start(worker),
               0);
    expect_int("queued CAD callback processed",
               wait_async_processed(1),
               1);
    expect_int("queued CAD callback probe", fake(&ctrl)->scan_count, 1);
    expect_int("queued CAD callback blocks send", sender.calls, 0);
    expect_int("queued CAD callback last result",
               daemon_tx_async_runtime_last_result(&last),
               1);
    expect_int("queued CAD callback result busy",
               last.tx_result,
               TX_RESULT_CAD_TIMEOUT);

    daemon_tx_async_runtime_shutdown();
}


static void test_queued_managed_cad_timeout_blocks_in_worker(void)
{
    RadioController ctrl;
    DataTxDaemonContext ctx;
    FakeSender sender;
    uint8_t payload[] = { 6, 7 };

    init_context(&ctrl, &ctx, &sender);
    ctrl.tx_queue_active.store(true);
    ctrl.mode = RADIO_MODE_LORA;
    ctrl.tx_mode = RADIO_TX_MODE_MANAGED;
    fake(&ctrl)->scan_state = 1;
    ctx.completion_seq = 55;

    expect_int("queued managed cad accepted",
               send_data_chunk(payload, sizeof(payload), 0, &ctx),
               0);
    expect_int("queued managed cad no frontdoor scan",
               fake(&ctrl)->scan_count <= 1 ? 1 : 0,
               1);
    expect_int("queued managed cad processed wait", wait_async_processed(1), 1);
    expect_int("queued managed cad worker probes", fake(&ctrl)->scan_count, 2);
    expect_int("queued managed cad timeout blocks send", sender.calls, 0);
    expect_size("queued managed cad stats no tx ok", ctrl.stats.tx_ok, 0);
    expect_size("queued managed cad stats no cadsend", ctrl.stats.cad_timeout_sends, 0);
    expect_size("queued managed cad stats cadtimeout", ctrl.stats.cad_timeouts, 1);

    DaemonTxJobResult completion;
    expect_int("queued managed cad completion pop",
               daemon_tx_async_runtime_pop_completion(&completion),
               0);
    expect_int("queued managed cad completion result",
               completion.tx_result,
               TX_RESULT_CAD_TIMEOUT);
    expect_int("queued managed cad timeout flag clear",
               completion.flags & FRAMED_DATA_TX_RESULT_FLAG_CAD_TIMEOUT,
               0);
}

static void test_direct_transmits_on_busy_channel_queued(void)
{
    RadioController ctrl;
    DataTxDaemonContext ctx;
    FakeSender sender;
    DaemonTxJob job;
    uint8_t payload[] = { 8 };

    init_context(&ctrl, &ctx, &sender);
    ctrl.tx_queue_active.store(true);
    ctrl.mode = RADIO_MODE_LORA;
    ctrl.tx_mode = RADIO_TX_MODE_DIRECT;
    fake(&ctrl)->scan_state = 1; // channel busy
    ctx.completion_seq = 56;

    // DIRECT disables CAD on the queued job, so the worker transmits immediately
    // without probing the busy channel.
    daemon_tx_job_init(&job, 433, RADIO_TX_MODE_DIRECT, 56);
    data_tx_configure_job_cad_policy(&ctx, &job);
    expect_int("queued direct cad disabled", (int)job.cad_enabled, 0);

    expect_int("queued direct accepted",
               send_data_chunk(payload, sizeof(payload), 0, &ctx),
               0);
    expect_int("queued direct processed wait", wait_async_processed(1), 1);
    expect_int("queued direct no worker probe", fake(&ctrl)->scan_count, 0);
    expect_int("queued direct sends", sender.calls, 1);
    expect_size("queued direct stats tx ok", ctrl.stats.tx_ok, 1);
    expect_size("queued direct stats no cadtimeout", ctrl.stats.cad_timeouts, 0);

    DaemonTxJobResult completion;
    expect_int("queued direct completion pop",
               daemon_tx_async_runtime_pop_completion(&completion),
               0);
    expect_int("queued direct completion result", completion.tx_result, TX_RESULT_OK);
    // P1: DIRECT must not advertise the managed flag.
    expect_int("queued direct managed flag clear",
               completion.flags & FRAMED_DATA_TX_RESULT_FLAG_MANAGED, 0);
}

static void test_managed_completion_sets_managed_flag(void)
{
    RadioController ctrl;
    DataTxDaemonContext ctx;
    FakeSender sender;
    uint8_t payload[] = { 7 };

    init_context(&ctrl, &ctx, &sender);
    ctrl.tx_queue_active.store(true);
    ctrl.mode = RADIO_MODE_LORA;
    ctrl.tx_mode = RADIO_TX_MODE_MANAGED;
    fake(&ctrl)->scan_state = 0; // channel free -> transmits

    expect_int("queued managed accepted",
               send_data_chunk(payload, sizeof(payload), 0, &ctx),
               0);
    expect_int("queued managed processed wait", wait_async_processed(1), 1);
    expect_int("queued managed sends", sender.calls, 1);

    DaemonTxJobResult completion;
    expect_int("queued managed completion pop",
               daemon_tx_async_runtime_pop_completion(&completion),
               0);
    // P1: MANAGED advertises the managed flag.
    expect_int("queued managed managed flag set",
               completion.flags & FRAMED_DATA_TX_RESULT_FLAG_MANAGED ? 1 : 0, 1);
}

static void test_runtime_switch_direct_then_managed(void)
{
    RadioController ctrl;
    DataTxDaemonContext ctx;
    FakeSender sender;
    uint8_t payload[] = { 7 };

    init_context(&ctrl, &ctx, &sender);
    ctrl.tx_queue_active.store(false);
    ctrl.mode = RADIO_MODE_LORA;
    fake(&ctrl)->scan_state = 1; // channel busy throughout

    // Runtime switch to DIRECT: send transmits immediately on a busy channel.
    ctrl.tx_mode = RADIO_TX_MODE_DIRECT;
    expect_int("switch direct transmits",
               send_data_chunk(payload, sizeof(payload), 0, &ctx),
               0);
    expect_int("switch direct sent once", sender.calls, 1);
    expect_int("switch direct no probe", fake(&ctrl)->scan_count, 0);

    // Runtime switch back to MANAGED: gating returns, busy channel drops the TX.
    ctrl.tx_mode = RADIO_TX_MODE_MANAGED;
    expect_int("switch managed blocks",
               send_data_chunk(payload, sizeof(payload), 0, &ctx),
               DAEMON_TX_OUTCOME_CHANNEL_BUSY);
    expect_int("switch managed no extra send", sender.calls, 1);
    expect_int("switch managed probed", fake(&ctrl)->scan_count > 0 ? 1 : 0, 1);
}


static void test_not_ready_still_short_circuits(void)
{
    RadioController ctrl;
    DataTxDaemonContext ctx;
    FakeSender sender;
    uint8_t payload[] = { 1 };

    init_context(&ctrl, &ctx, &sender);
    ctrl.tx_queue_active.store(true);
    ctrl.health = RADIO_HEALTH_FAILED;

    expect_int("not ready tx result",
               send_data_chunk(payload, sizeof(payload), 0, &ctx),
               DAEMON_TX_OUTCOME_RADIO_NOT_READY);
    expect_int("not ready no send", sender.calls, 0);
    expect_size("not ready no queued", daemon_tx_async_runtime_pending(), 0);
}

static void test_controller_cad_policy_snapshot(void)
{
    RadioController ctrl;
    DataTxDaemonContext ctx;
    FakeSender sender;
    DaemonTxJob job;

    init_context(&ctrl, &ctx, &sender);
    ctrl.mode    = RADIO_MODE_LORA;
    ctrl.tx_mode = RADIO_TX_MODE_MANAGED;

    ctrl.cad_wait_timeout_ms.store(300u);
    ctrl.cad_idle_stable_ms.store(100u);
    ctrl.cad_poll_interval_ms.store(50u);
    ctrl.cad_send_after_timeout.store(true);

    daemon_tx_job_init(&job, 433, RADIO_TX_MODE_MANAGED, 80);
    data_tx_configure_job_cad_policy(&ctx, &job);

    /* wait_ticks  = ceil(300/50) = 6 */
    expect_int("cad policy snapshot wait ticks",
               (int)job.cad_wait_ticks, 6);
    /* idle_ticks  = ceil(100/50) = 2 */
    expect_int("cad policy snapshot idle ticks",
               (int)job.cad_idle_stable_ticks, 2);
    /* poll_usec   = 50 * 1000 = 50000 */
    expect_int("cad policy snapshot poll usec",
               (int)job.cad_poll_interval_usec, 50000);
    expect_int("cad policy snapshot timeout send",
               (int)job.cad_send_after_timeout, 1);

    daemon_tx_async_runtime_shutdown();
}

/*
 * Harness contract item 4. CADTXAFTERTIMEOUT is an opt-in to transmit after a
 * sequence of VALID busy observations. A hardware CAD error is not an
 * observation at all, and it must never be flattened into one -- otherwise a
 * radio whose scan is broken transmits on every attempt, which is the exact
 * failure the register-CAD deadline exists to surface rather than hide.
 *
 * The scan returns a negative RadioLib error here, which
 * radio_cad_status_from_scan_state() maps to UNAVAILABLE.
 */
/*
 * A completed packet must survive the WHOLE synchronous send, not only the
 * probe.
 *
 * Found by an upstream diff review, and it is the caller's half of the race the
 * probe already handles. The probe correctly preserves the packet and reports
 * BUSY -- but the synchronous CAD wait runs on the main thread, which is also
 * the thread that drains RX, and it drains only AFTER socket processing. So the
 * packet cannot be collected while this loop spins: every probe sees it, every
 * probe says BUSY, the CAD budget is exhausted, and with CADTXAFTERTIMEOUT=1
 * the timeout then permits a send whose TX preparation clears `received` and
 * the IRQ flags. The packet no client ever saw is gone.
 *
 * Measured before the fix, through the production send path:
 *   outcome=0 (OK), transmissions=1, undrained RX clears=1, received=0
 *
 * Both timeout policies are covered, because the opt-in is what turns a
 * preserved packet into a destroyed one.
 */
static void test_pending_rx_survives_synchronous_managed_tx(void)
{
    for (int opt_in = 0; opt_in <= 1; opt_in++) {
        RadioController ctrl;
        DataTxDaemonContext ctx;
        FakeSender sender;
        uint8_t payload[] = { 1, 2, 3 };
        char name[128];

        init_context(&ctrl, &ctx, &sender);
        ctrl.mode = RADIO_MODE_LORA;
        ctrl.tx_mode = RADIO_TX_MODE_MANAGED;
        ctrl.tx_queue_active.store(false);      /* the synchronous path */
        ctrl.cad_scan_available = true;
        ctrl.cad_wait_timeout_ms.store(50u);
        ctrl.cad_poll_interval_ms.store(10u);
        ctrl.cad_idle_stable_ms.store(0u);
        ctrl.cad_send_after_timeout.store(opt_in != 0);
        fake(&ctrl)->rx_completes_on_standby = true;

        const int outcome = send_data_chunk(payload, sizeof(payload), 0, &ctx);

        /* Only assertions that can actually fail in this harness. This test
         * injects a fake sender, so the production TX preparation -- the code
         * that clears `received` and the IRQ flags, i.e. the step that destroys
         * the packet -- is never reached here, and counting driver transmits or
         * IRQ clears would assert nothing. Those are covered by the upstream
         * review's reproduction, which links the production lora_send(). What
         * this test owns is the DECISION: the send must be refused before the
         * timeout opt-in can permit it. */
        snprintf(name, sizeof(name),
                 "CADTXAFTERTIMEOUT=%d: it is still flagged for the main loop",
                 opt_in);
        expect_int(name, ctrl.received.load() ? 1 : 0, 1);

        snprintf(name, sizeof(name),
                 "CADTXAFTERTIMEOUT=%d: the send is refused, not reported OK",
                 opt_in);
        expect_int(name, outcome != DAEMON_TX_OUTCOME_OK ? 1 : 0, 1);

        snprintf(name, sizeof(name),
                 "CADTXAFTERTIMEOUT=%d: and the sender was never called",
                 opt_in);
        expect_int(name, sender.calls, 0);

        daemon_tx_async_runtime_shutdown();
    }
}

static void test_cad_error_is_never_converted_into_a_transmission(void)
{
    RadioController ctrl;
    DataTxDaemonContext ctx;
    FakeSender sender;

    init_context(&ctrl, &ctx, &sender);
    ctrl.mode = RADIO_MODE_LORA;
    ctrl.tx_mode = RADIO_TX_MODE_MANAGED;
    ctrl.cad_send_after_timeout.store(true);      /* the opt-in is ON */
    fake(&ctrl)->scan_state = RADIOLIB_ERR_RX_TIMEOUT;  /* the scan is broken */

    expect_int("a CAD error ends the attempt even with the opt-in on",
               data_tx_wait_channel_free_with_limits_ex(&ctx, 4, 1, 0, true),
               DATA_TX_CAD_WAIT_ERROR);

    /* And it stops at the FIRST error rather than burning the whole window. */
    expect_int("the error is not retried to the end of the window",
               fake(&ctrl)->scan_count, 1);

    /* Contrast: a genuinely busy channel with the same opt-in still sends. */
    fake(&ctrl)->scan_count = 0;
    fake(&ctrl)->scan_state = 1;                  /* busy, a valid reading */
    expect_int("a genuinely busy channel still follows the opt-in",
               data_tx_wait_channel_free_with_limits_ex(&ctx, 2, 1, 0, true),
               DATA_TX_CAD_WAIT_TIMEOUT_SEND);

    daemon_tx_async_runtime_shutdown();
}

/*
 * Harness contract item 12. There are two CAD wait loops -- the synchronous one
 * in daemon_data_tx_runtime.cpp and the executor's, used by the queued worker.
 * They must reach the same decision from the same scripted sequence, or the
 * TXQUEUE setting quietly changes listen-before-talk behaviour.
 *
 * This is a parity check, not a merge: one round of it is worth more than a
 * framework built to unify two loops that are each simple.
 */
static void test_sync_and_queued_cad_paths_agree(void)
{
    struct Case {
        const char *name;
        int pattern[4];
        int pattern_len;
        bool opt_in;
        int sync_expected;       /* DATA_TX_CAD_WAIT_* */
        int queued_expected;     /* DAEMON_TX_OUTCOME_* */
    } cases[] = {
        { "free", { 0, 0, 0, 0 }, 4, false,
          DATA_TX_CAD_WAIT_FREE, DAEMON_TX_OUTCOME_OK },
        { "busy without the opt-in", { 1, 1, 1, 1 }, 4, false,
          DATA_TX_CAD_WAIT_BLOCK, DAEMON_TX_OUTCOME_CHANNEL_BUSY },
        { "busy with the opt-in", { 1, 1, 1, 1 }, 4, true,
          DATA_TX_CAD_WAIT_TIMEOUT_SEND, DAEMON_TX_OUTCOME_OK },
        { "error with the opt-in", { RADIOLIB_ERR_RX_TIMEOUT, 0, 0, 0 }, 4, true,
          DATA_TX_CAD_WAIT_ERROR, DAEMON_TX_OUTCOME_RADIO_ERROR },
        { "busy then free", { 1, 1, 0, 0 }, 4, false,
          DATA_TX_CAD_WAIT_FREE, DAEMON_TX_OUTCOME_OK },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const Case &c = cases[i];
        char name[128];

        /* --- the synchronous path --- */
        {
            RadioController ctrl;
            DataTxDaemonContext ctx;
            FakeSender sender;

            init_context(&ctrl, &ctx, &sender);
            ctrl.mode = RADIO_MODE_LORA;
            ctrl.tx_mode = RADIO_TX_MODE_MANAGED;
            memcpy(fake(&ctrl)->scan_pattern, c.pattern, sizeof(c.pattern));
            fake(&ctrl)->scan_pattern_len = c.pattern_len;
            fake(&ctrl)->scan_state = c.pattern[c.pattern_len - 1];

            snprintf(name, sizeof(name), "sync path: %s", c.name);
            expect_int(name,
                       data_tx_wait_channel_free_with_limits_ex(
                           &ctx, 4, 1, 0, c.opt_in),
                       c.sync_expected);
            daemon_tx_async_runtime_shutdown();
        }

        /* --- the queued worker's path, same script --- */
        {
            RadioController ctrl;
            DataTxDaemonContext ctx;
            FakeSender sender;
            DaemonTxJob job;
            uint8_t payload[] = { 7 };

            init_context(&ctrl, &ctx, &sender);
            ctrl.mode = RADIO_MODE_LORA;
            ctrl.tx_mode = RADIO_TX_MODE_MANAGED;
            memcpy(fake(&ctrl)->scan_pattern, c.pattern, sizeof(c.pattern));
            fake(&ctrl)->scan_pattern_len = c.pattern_len;
            fake(&ctrl)->scan_state = c.pattern[c.pattern_len - 1];

            daemon_tx_job_init(&job, 433, RADIO_TX_MODE_MANAGED, 90);
            daemon_tx_job_set_payload(&job, payload, sizeof(payload));
            /* Without this the executor skips the CAD loop entirely and every
             * case "passes" as a plain send -- which is how the first version
             * of this test went green on two cases it was not exercising. */
            job.cad_enabled = 1;
            job.cad_wait_ticks = 4;
            job.cad_idle_stable_ticks = 1;
            job.cad_poll_interval_usec = 0;
            job.cad_send_after_timeout = c.opt_in ? 1 : 0;

            DaemonTxJobResult result =
                daemon_tx_execute_job_with_sender_and_cad(
                    &job, fake_send, &sender,
                    daemon_data_tx_worker_cad_probe, &ctrl,
                    daemon_data_tx_worker_cad_sleep, NULL);

            snprintf(name, sizeof(name), "queued path: %s", c.name);
            expect_int(name, (int)result.outcome, c.queued_expected);
            daemon_tx_async_runtime_shutdown();
        }
    }
}

static void test_managed_busy_timeout_send_when_opt_in(void)
{
    RadioController ctrl;
    DataTxDaemonContext ctx;
    FakeSender sender;
    DaemonTxJob job;

    init_context(&ctrl, &ctx, &sender);
    ctrl.mode = RADIO_MODE_LORA;
    ctrl.tx_mode = RADIO_TX_MODE_MANAGED;
    fake(&ctrl)->scan_state = 1;   /* channel stays busy */

    /* Opt-in: nach CAD-Timeout trotzdem senden. */
    expect_int("managed busy timeout sends when opt-in",
               data_tx_wait_channel_free_with_limits_ex(&ctx, 2, 3, 0, true),
               DATA_TX_CAD_WAIT_TIMEOUT_SEND);
    expect_int("managed busy timeout send probe count", fake(&ctrl)->scan_count, 2);

    /* The queued snapshot carries the opt-in too. */
    ctrl.cad_send_after_timeout.store(true);
    daemon_tx_job_init(&job, 433, RADIO_TX_MODE_MANAGED, 81);
    data_tx_configure_job_cad_policy(&ctx, &job);
    expect_int("managed queued timeout send opt-in",
               job.cad_send_after_timeout, 1);

    daemon_tx_async_runtime_shutdown();
}

static void test_queue_long_wait_does_not_starve_later_job(void)
{
    RadioController ctrl;
    DataTxDaemonContext ctx;
    FakeSender sender;
    uint8_t payload_a[] = { 1 };
    uint8_t payload_b[] = { 2 };

    init_context(&ctrl, &ctx, &sender);
    ctrl.tx_queue_active.store(true);
    ctrl.mode = RADIO_MODE_LORA;
    ctrl.tx_mode = RADIO_TX_MODE_MANAGED;
    fake(&ctrl)->scan_state = 1;   /* channel busy for both jobs */

    /* Job A runs into a bounded CAD timeout, not 20 s. */
    ctx.completion_seq = 60;
    expect_int("starve guard job A accepted",
               send_data_chunk(payload_a, sizeof(payload_a), 0, &ctx),
               0);

    /* Job B direkt dahinter. */
    ctx.completion_seq = 61;
    expect_int("starve guard job B accepted",
               send_data_chunk(payload_b, sizeof(payload_b), 0, &ctx),
               0);

    /* Both must be processed: A does not block B. */
    expect_int("starve guard both processed", wait_async_processed(2), 1);
    expect_size("starve guard processed count",
                daemon_tx_async_runtime_processed(), 2);

    /* Bounded per job by cad_wait_ticks (2) -> total probes bounded. */
    expect_int("starve guard bounded probes",
               fake(&ctrl)->scan_count <= 4 ? 1 : 0, 1);
    expect_int("starve guard no transmit on busy", sender.calls, 0);

    daemon_tx_async_runtime_shutdown();
}

int main(int argc, char **argv)
{
    /* daemon_data_tx_context() reads log tags from the band descriptor. */
    daemon_band_resolve(RADIO_BAND_433);

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

    test_default_direct_path();
    test_fsk_payload_boundary();
    test_lora_keeps_the_full_payload();
    test_direct_refuses_residual_async_work();
    test_txqueue_optin_path();
    test_txqueue_direct_full_rejects_newest();
    test_direct_tx_busy_timeout_blocks_send();
    test_queued_tx_skips_frontdoor_tx_busy_wait();
    test_tx_busy_wait_clears();
    test_direct_transmits_on_busy_channel_sync();
    test_managed_busy_timeout_policy_blocks_by_default();
    test_managed_free_waits_for_stable_idle();
    test_managed_busy_resets_stable_idle();
    test_cad_timeout_sets_result_flag();
    test_cad_free_does_not_set_timeout_flag();
    test_worker_cad_mode_check_uses_radio_lock();
    test_queued_cad_callback_survives_later_non_cad_config();
    test_queued_managed_cad_timeout_blocks_in_worker();
    test_direct_transmits_on_busy_channel_queued();
    test_managed_completion_sets_managed_flag();
    test_runtime_switch_direct_then_managed();
    test_not_ready_still_short_circuits();
    test_controller_cad_policy_snapshot();
    test_pending_rx_survives_synchronous_managed_tx();
    test_cad_error_is_never_converted_into_a_transmission();
    test_sync_and_queued_cad_paths_agree();
    test_managed_busy_timeout_send_when_opt_in();
    test_queue_long_wait_does_not_starve_later_job();

    daemon_tx_async_runtime_shutdown();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
