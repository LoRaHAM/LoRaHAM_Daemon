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

/* --- Dispatch behavior --- */

static void test_dispatch_ready_client(void)
{
    int sv[2];
    int clients[2] = {0};
    uint8_t buf[buf_SIZE];
    EventLoopSet set;
    EventLoopReadySet readfds;
    FakeRadio radio;
    volatile RadioMode_t mode = RADIO_MODE_LORA;
    volatile bool getrssi_active = false;

    memset(&g_apply_state, 0, sizeof(g_apply_state));
    memset(buf, 0, sizeof(buf));

    if (!make_socket_pair(sv)) {
        g_fail++;
        return;
    }

    clients[0] = sv[1];

    const char *cmd = "SET GETRSSI=1";
    write(sv[0], cmd, strlen(cmd));

    event_loop_reset(&set);
    event_loop_add_fd(&set, sv[1]);
    expect_int("ready wait", event_loop_wait(&set, &readfds, 100000), 1);

    ConfigDispatchContext<FakeRadio> ctx = {
        clients,
        &radio,
        "CONF TEST",
        "[TEST]",
        &mode,
        &getrssi_active,
        record_apply_config,
        fake_rx_callback
    };

    config_dispatch_context<FakeRadio>(&ctx, 2, &readfds, buf);

    expect_int("apply called once", g_apply_state.calls, 1);
    expect_str("apply tag", g_apply_state.tag, "CONF TEST");
    expect_str("apply cmd", g_apply_state.cmd, "SET GETRSSI=1");
    expect_int("mode updated by apply", mode == RADIO_MODE_FSK, 1);
    expect_int("getrssi updated by apply", getrssi_active == true, 1);
    expect_int("callback restored", radio.callback_count, 1);
    expect_int("startReceive called", radio.start_receive_count, 1);
    expect_int("client still open", clients[0] > 0, 1);

    close(sv[0]);
    client_set_close_slot(clients, 0);
}


static void test_dispatch_ready_client_epoll(void)
{
    int sv[2];
    int clients[2] = {0};
    uint8_t buf[buf_SIZE];
    EventLoopSet set;
    EventLoopReadySet readfds;
    FakeRadio radio;
    volatile RadioMode_t mode = RADIO_MODE_LORA;
    volatile bool getrssi_active = false;

    memset(&g_apply_state, 0, sizeof(g_apply_state));
    memset(buf, 0, sizeof(buf));

    if (!make_socket_pair(sv)) {
        g_fail++;
        return;
    }

    clients[0] = sv[1];

    if (event_loop_init_epoll(&set) != 0) {
        close(sv[0]);
        client_set_close_slot(clients, 0);
        g_fail++;
        printf("[FAIL] config epoll init\n");
        return;
    }

    const char *cmd = "SET GETRSSI=1";
    write(sv[0], cmd, strlen(cmd));

    event_loop_add_fd(&set, sv[1]);
    expect_int("ready epoll wait", event_loop_wait(&set, &readfds, 100000), 1);

    ConfigDispatchContext<FakeRadio> ctx = {
        clients,
        &radio,
        "CONF TEST",
        "[TEST]",
        &mode,
        &getrssi_active,
        record_apply_config,
        fake_rx_callback
    };

    config_dispatch_context<FakeRadio>(&ctx, 2, &readfds, buf);

    expect_int("epoll apply called once", g_apply_state.calls, 1);
    expect_str("epoll apply cmd", g_apply_state.cmd, "SET GETRSSI=1");
    expect_int("epoll callback restored", radio.callback_count, 1);
    expect_int("epoll startReceive called", radio.start_receive_count, 1);

    event_loop_close(&set);
    close(sv[0]);
    client_set_close_slot(clients, 0);
}

static void test_dispatch_ignores_not_ready_client(void)
{
    int sv[2];
    int clients[2] = {0};
    uint8_t buf[buf_SIZE];
    EventLoopSet set;
    EventLoopReadySet readfds;
    FakeRadio radio;
    volatile RadioMode_t mode = RADIO_MODE_LORA;
    volatile bool getrssi_active = false;

    memset(&g_apply_state, 0, sizeof(g_apply_state));
    memset(buf, 0, sizeof(buf));

    if (!make_socket_pair(sv)) {
        g_fail++;
        return;
    }

    clients[0] = sv[1];

    event_loop_reset(&set);
    event_loop_add_fd(&set, sv[1]);
    expect_int("not ready wait", event_loop_wait(&set, &readfds, 1000), 0);

    ConfigDispatchContext<FakeRadio> ctx = {
        clients,
        &radio,
        "CONF TEST",
        NULL,
        &mode,
        &getrssi_active,
        record_apply_config,
        fake_rx_callback
    };

    config_dispatch_context<FakeRadio>(&ctx, 2, &readfds, buf);

    expect_int("not ready apply count", g_apply_state.calls, 0);
    expect_int("not ready callback count", radio.callback_count, 0);
    expect_int("not ready startReceive count", radio.start_receive_count, 0);
    expect_int("not ready client open", clients[0] > 0, 1);

    close(sv[0]);
    client_set_close_slot(clients, 0);
}

static void test_dispatch_closes_eof_client(void)
{
    int sv[2];
    int clients[2] = {0};
    uint8_t buf[buf_SIZE];
    EventLoopSet set;
    EventLoopReadySet readfds;
    FakeRadio radio;
    volatile RadioMode_t mode = RADIO_MODE_LORA;
    volatile bool getrssi_active = false;

    memset(&g_apply_state, 0, sizeof(g_apply_state));
    memset(buf, 0, sizeof(buf));

    if (!make_socket_pair(sv)) {
        g_fail++;
        return;
    }

    clients[0] = sv[1];

    close(sv[0]);

    event_loop_reset(&set);
    event_loop_add_fd(&set, sv[1]);
    expect_int("eof wait", event_loop_wait(&set, &readfds, 100000), 1);

    ConfigDispatchContext<FakeRadio> ctx = {
        clients,
        &radio,
        "CONF TEST",
        NULL,
        &mode,
        &getrssi_active,
        record_apply_config,
        fake_rx_callback
    };

    config_dispatch_context<FakeRadio>(&ctx, 2, &readfds, buf);

    expect_int("eof apply count", g_apply_state.calls, 0);
    expect_int("eof callback count", radio.callback_count, 0);
    expect_int("eof startReceive count", radio.start_receive_count, 0);
    expect_int("eof client closed", clients[0], 0);
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
