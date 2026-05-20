#include "../config_dispatch.h"

#include <stdio.h>
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

/* --- Fake radio --- */

struct FakeRadio {
    int callback_count;
    int start_receive_count;
    void (*last_callback)(void);

    FakeRadio() : callback_count(0), start_receive_count(0), last_callback(NULL) {}

    void setPacketReceivedAction(void (*cb)(void))
    {
        last_callback = cb;
        callback_count++;
    }

    void startReceive()
    {
        start_receive_count++;
    }
};

/* --- Test state --- */

typedef struct {
    int calls;
    char tag[32];
    char cmd[128];
} ApplyState;

static ApplyState g_apply_state;

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

static int make_socket_pair(int sv[2])
{
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        perror("socketpair");
        return 0;
    }

    return 1;
}

/* --- Apply callback used by tests --- */

static void record_apply_config(FakeRadio& radio,
                                const char *tag,
                                const char *cmd,
                                volatile RadioMode_t& mode,
                                volatile bool& getrssi_active)
{
    (void)radio;

    g_apply_state.calls++;
    snprintf(g_apply_state.tag, sizeof(g_apply_state.tag), "%s", tag);
    snprintf(g_apply_state.cmd, sizeof(g_apply_state.cmd), "%s", cmd);

    mode = RADIO_MODE_FSK;
    getrssi_active = true;
}

static void init_fake_controller(RadioController<FakeRadio> *ctrl,
                                 RadioHealth health)
{
    radio_controller_init(ctrl,
                          RADIO_BAND_433,
                          "TEST",
                          false,
                          fake_rx_callback,
                          13);
    ctrl->radio.reset(new FakeRadio());
    ctrl->health = health;
}

static ConfigDispatchContext<FakeRadio> make_context(ClientSlot *slots,
                                                     RadioController<FakeRadio> *ctrl)
{
    ConfigDispatchLog log = {
        NULL,
        NULL,
        NULL
    };

    ConfigDispatchContext<FakeRadio> ctx = {
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
    RadioController<FakeRadio> ctrl;
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
    write(sv[0], cmd, strlen(cmd));

    event_loop_reset(&set);
    event_loop_add_fd(&set, sv[1]);
    expect_int("ready wait", event_loop_wait(&set, &readfds, 100000), 1);

    ConfigDispatchContext<FakeRadio> ctx =
        make_context(slots, &ctrl);

    config_dispatch_context<FakeRadio>(&ctx, 2, &readfds, buf);

    expect_int("apply called once", g_apply_state.calls, 1);
    expect_str("apply tag", g_apply_state.tag, "CONF TEST");
    expect_str("apply cmd", g_apply_state.cmd, "SET GETRSSI=1");
    expect_int("mode updated by apply", ctrl.mode == RADIO_MODE_FSK, 1);
    expect_int("getrssi updated by apply", ctrl.getrssi_active == true, 1);
    expect_int("callback restored", ctrl.radio->callback_count, 1);
    expect_int("startReceive called", ctrl.radio->start_receive_count, 1);
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
    RadioController<FakeRadio> ctrl;
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
    write(sv[0], cmd, strlen(cmd));

    event_loop_add_fd(&set, sv[1]);
    expect_int("ready epoll wait", event_loop_wait(&set, &readfds, 100000), 1);

    ConfigDispatchContext<FakeRadio> ctx =
        make_context(slots, &ctrl);

    config_dispatch_context<FakeRadio>(&ctx, 2, &readfds, buf);

    expect_int("epoll apply called once", g_apply_state.calls, 1);
    expect_str("epoll apply cmd", g_apply_state.cmd, "SET GETRSSI=1");
    expect_int("epoll callback restored", ctrl.radio->callback_count, 1);
    expect_int("epoll startReceive called", ctrl.radio->start_receive_count, 1);

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
    RadioController<FakeRadio> ctrl;
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
    write(sv[0], cmd, strlen(cmd));

    event_loop_reset(&set);
    event_loop_add_fd(&set, sv[1]);
    expect_int("not ready wait", event_loop_wait(&set, &readfds, 100000), 1);

    ConfigDispatchContext<FakeRadio> ctx =
        make_context(slots, &ctrl);

    config_dispatch_context<FakeRadio>(&ctx, 2, &readfds, buf);

    expect_int("not ready apply count", g_apply_state.calls, 0);
    expect_int("not ready callback count", ctrl.radio->callback_count, 0);
    expect_int("not ready startReceive count", ctrl.radio->start_receive_count, 0);
    expect_int("not ready client open", client_slot_has_client(&slots[0]), 1);

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
    RadioController<FakeRadio> ctrl;
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

    ConfigDispatchContext<FakeRadio> ctx =
        make_context(slots, &ctrl);

    config_dispatch_context<FakeRadio>(&ctx, 2, &readfds, buf);

    expect_int("eof apply count", g_apply_state.calls, 0);
    expect_int("eof callback count", ctrl.radio->callback_count, 0);
    expect_int("eof startReceive count", ctrl.radio->start_receive_count, 0);
    expect_int("eof client closed", slots[0].fd, 0);
    expect_size("eof output reset", client_output_queue_pending(&slots[0].output), 0);
    expect_size("eof stream reset", slots[0].stream.len, 0);

    event_loop_close(&set);
}

/* --- CLI parsing and test sequence --- */

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
    test_dispatch_ready_client_epoll();
    test_dispatch_ignores_not_ready_client();
    test_dispatch_closes_eof_client();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
