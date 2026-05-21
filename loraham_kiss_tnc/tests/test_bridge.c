#include "ax25.h"
#include "kiss.h"
#include "loraham_kiss_tnc.h"
#include "loraham_sock.h"
#include "tnc2.h"

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>

int lhkt_test_send_tnc2_to_kiss_client(int client_fd,
                                       const char *tnc2,
                                       lhkt_stats_t *stats);

int lhkt_test_add_fd_to_set(int fd);

int lhkt_test_handle_kiss_frame(const kiss_frame_t *kiss_frame,
                                kiss_params_t *kiss_params,
                                const lhkt_config_t *cfg,
                                lhkt_stats_t *stats,
                                int data_fd);


void lhkt_test_bridge_reset_tx_hooks(void);
void lhkt_test_bridge_set_config_results(const int *results, size_t count);
size_t lhkt_test_bridge_config_call_count(void);
double lhkt_test_bridge_config_freq_at(size_t index);
void lhkt_test_bridge_set_write_result(ssize_t result);
size_t lhkt_test_bridge_write_call_count(void);
size_t lhkt_test_bridge_sleep_call_count(void);
int lhkt_test_bridge_should_reconnect_data_socket(int ret);
int lhkt_test_bridge_should_disconnect_kiss_client(int ret);
void lhkt_test_bridge_disconnect_data_socket(int *data_fd,
                                             loraham_rx_state_t *rx_state,
                                             lhkt_stats_t *stats);
void lhkt_test_bridge_reset_stop(void);
void lhkt_test_bridge_request_stop(void);
int lhkt_test_bridge_should_stop(void);
int lhkt_test_bridge_wait_fd_writable(int fd);
int lhkt_test_bridge_sleep_ms(int ms);

static void test_fd_set_rejects_too_large_fd(void)
{
    assert(lhkt_test_add_fd_to_set(FD_SETSIZE) == LHKT_ERR_LONG);
}


static void test_shutdown_stop_flag(void)
{
    lhkt_test_bridge_reset_stop();
    assert(lhkt_test_bridge_should_stop() == 0);

    lhkt_test_bridge_request_stop();
    assert(lhkt_test_bridge_should_stop() == 1);

    lhkt_test_bridge_reset_stop();
    assert(lhkt_test_bridge_should_stop() == 0);
}


static void test_stop_aware_wait_helpers(void)
{
    int sv[2];

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    lhkt_test_bridge_reset_stop();
    assert(lhkt_test_bridge_sleep_ms(1) == LHKT_OK);

    lhkt_test_bridge_request_stop();
    assert(lhkt_test_bridge_sleep_ms(1) == LHKT_ERR);
    assert(lhkt_test_bridge_wait_fd_writable(sv[0]) == LHKT_ERR);

    lhkt_test_bridge_reset_stop();

    close(sv[0]);
    close(sv[1]);
}


static void make_valid_kiss_frame(kiss_frame_t *frame)
{
    ax25_frame_t ax25;
    uint8_t raw[LHKT_AX25_MAX_FRAME];
    size_t raw_len = 0;
    const char *payload = "hello";

    assert(frame != NULL);

    ax25_frame_init(&ax25);
    assert(ax25_addr_parse("APRS", &ax25.dst) == LHKT_OK);
    assert(ax25_addr_parse("DJ0CHE-10", &ax25.src) == LHKT_OK);

    memcpy(ax25.payload, payload, strlen(payload));
    ax25.payload_len = strlen(payload);

    assert(ax25_encode_ui(&ax25, raw, sizeof(raw), &raw_len) == LHKT_OK);

    memset(frame, 0, sizeof(*frame));
    frame->port = 0;
    frame->command = KISS_CMD_DATA;
    memcpy(frame->data, raw, raw_len);
    frame->data_len = raw_len;
}

static void test_tnc2_to_kiss_output(void)
{
    int sv[2];
    ssize_t n;
    uint8_t buf[LHKT_KISS_MAX_FRAME];
    kiss_decoder_t dec;
    kiss_frame_t kiss_frame;
    ax25_frame_t ax25;
    char out[LHKT_TNC2_MAX_LINE];
    size_t out_len = 0;
    int ret;
    int got_frame = 0;
    size_t i;
    lhkt_stats_t stats;
    const char *tnc2 = "DJ0CHE-10>APRS:hi";

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    lhkt_stats_init(&stats);

    assert(lhkt_test_send_tnc2_to_kiss_client(sv[0], tnc2, &stats) == LHKT_OK);

    n = read(sv[1], buf, sizeof(buf));
    assert(n > 0);

    kiss_decoder_init(&dec);

    for (i = 0; i < (size_t)n; i++) {
        ret = kiss_decode_byte(&dec, buf[i], &kiss_frame);
        if (ret == 1) {
            got_frame++;
        } else {
            assert(ret == 0);
        }
    }

    assert(got_frame == 1);
    assert(kiss_frame.command == KISS_CMD_DATA);
    assert(kiss_frame.port == 0);

    assert(ax25_decode_ui(kiss_frame.data, kiss_frame.data_len, &ax25) == LHKT_OK);
    assert(tnc2_format_line(&ax25, out, sizeof(out), &out_len) == LHKT_OK);
    assert(strcmp(out, tnc2) == 0);
    assert(out_len == strlen(out));

    assert(stats.tnc2_rx == 1);
    assert(stats.ax25_tx == 1);
    assert(stats.kiss_tx == 1);
    assert(stats.tnc2_drop == 0);
    assert(stats.ax25_drop == 0);
    assert(stats.kiss_drop == 0);

    close(sv[0]);
    close(sv[1]);
}


static void test_nonzero_kiss_port_is_dropped(void)
{
    lhkt_config_t cfg;
    lhkt_stats_t stats;
    kiss_params_t params;
    kiss_frame_t frame;

    lhkt_config_defaults(&cfg);
    lhkt_stats_init(&stats);
    kiss_params_init(&params);
    memset(&frame, 0, sizeof(frame));

    frame.port = 1;
    frame.command = KISS_CMD_DATA;
    frame.data_len = 0;

    assert(lhkt_test_handle_kiss_frame(&frame,
                                       &params,
                                       &cfg,
                                       &stats,
                                       -1) == LHKT_ERR_UNSUPPORTED);

    assert(stats.kiss_drop == 1);
    assert(stats.ax25_rx == 0);
    assert(stats.loraham_tx == 0);
}

static void test_invalid_tnc2_is_dropped(void)
{
    int sv[2];
    lhkt_stats_t stats;

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    lhkt_stats_init(&stats);

    assert(lhkt_test_send_tnc2_to_kiss_client(sv[0], "not a tnc2 line", &stats) == LHKT_ERR_FORMAT);
    assert(stats.tnc2_drop == 1);
    assert(stats.kiss_tx == 0);

    close(sv[0]);
    close(sv[1]);
}


static void test_client_write_failure_returns_socket_error(void)
{
    int sv[2];
    lhkt_stats_t stats;

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    lhkt_stats_init(&stats);

    close(sv[1]);

    assert(lhkt_test_send_tnc2_to_kiss_client(sv[0],
                                              "DJ0CHE-10>APRS:hi",
                                              &stats) == LHKT_ERR_CLIENT_SOCKET);

    assert(lhkt_test_bridge_should_disconnect_kiss_client(LHKT_ERR_CLIENT_SOCKET) == 1);
    assert(lhkt_test_bridge_should_disconnect_kiss_client(LHKT_ERR_FORMAT) == 0);
    assert(lhkt_test_bridge_should_disconnect_kiss_client(LHKT_ERR_UNSUPPORTED) == 0);
    assert(lhkt_test_bridge_should_reconnect_data_socket(LHKT_ERR_CLIENT_SOCKET) == 0);

    assert(stats.tnc2_rx == 1);
    assert(stats.ax25_tx == 1);
    assert(stats.kiss_tx == 0);
    assert(stats.kiss_drop == 1);

    close(sv[0]);
}


static void test_tx_write_failure_restores_rx(void)
{
    lhkt_config_t cfg;
    lhkt_stats_t stats;
    kiss_params_t params;
    kiss_frame_t frame;
    int config_results[] = { LHKT_OK, LHKT_OK };

    lhkt_config_defaults(&cfg);
    lhkt_stats_init(&stats);
    kiss_params_init(&params);
    make_valid_kiss_frame(&frame);

    lhkt_test_bridge_reset_tx_hooks();
    lhkt_test_bridge_set_config_results(config_results,
                                        sizeof(config_results) / sizeof(config_results[0]));
    lhkt_test_bridge_set_write_result(-1);

    assert(lhkt_test_handle_kiss_frame(&frame, &params, &cfg, &stats, 42) == LHKT_ERR_TX_SOCKET);
    assert(lhkt_test_bridge_should_reconnect_data_socket(LHKT_ERR_TX_SOCKET) == 1);
    assert(lhkt_test_bridge_should_reconnect_data_socket(LHKT_ERR_FORMAT) == 0);
    assert(lhkt_test_bridge_should_reconnect_data_socket(LHKT_ERR_UNSUPPORTED) == 0);

    assert(lhkt_test_bridge_write_call_count() == 1);
    assert(lhkt_test_bridge_config_call_count() == 2);
    assert(lhkt_test_bridge_config_freq_at(0) > 433.899);
    assert(lhkt_test_bridge_config_freq_at(0) < 433.901);
    assert(lhkt_test_bridge_config_freq_at(1) > 433.774);
    assert(lhkt_test_bridge_config_freq_at(1) < 433.776);
    assert(stats.loraham_drop == 1);
    assert(stats.tx_restore_failures == 0);
    assert(stats.loraham_tx == 0);
}


static void test_shutdown_during_tx_settle_restores_rx(void)
{
    lhkt_config_t cfg;
    lhkt_stats_t stats;
    kiss_params_t params;
    kiss_frame_t frame;
    int config_results[] = { LHKT_OK, LHKT_OK };

    lhkt_config_defaults(&cfg);
    lhkt_stats_init(&stats);
    kiss_params_init(&params);
    make_valid_kiss_frame(&frame);

    lhkt_test_bridge_reset_tx_hooks();
    lhkt_test_bridge_set_config_results(config_results,
                                        sizeof(config_results) / sizeof(config_results[0]));

    lhkt_test_bridge_request_stop();

    assert(lhkt_test_handle_kiss_frame(&frame, &params, &cfg, &stats, 42) == LHKT_ERR);

    assert(lhkt_test_bridge_write_call_count() == 0);
    assert(lhkt_test_bridge_config_call_count() == 2);
    assert(lhkt_test_bridge_config_freq_at(0) > 433.899);
    assert(lhkt_test_bridge_config_freq_at(0) < 433.901);
    assert(lhkt_test_bridge_config_freq_at(1) > 433.774);
    assert(lhkt_test_bridge_config_freq_at(1) < 433.776);
    assert(stats.loraham_tx == 0);

    lhkt_test_bridge_reset_stop();
}


static void test_tx_socket_error_invalidates_data_socket(void)
{
    int sv[2];
    int data_fd;
    lhkt_stats_t stats;
    loraham_rx_state_t rx_state;

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    data_fd = sv[0];

    lhkt_stats_init(&stats);
    loraham_rx_state_init(&rx_state);
    rx_state.seen_header = 1;
    rx_state.len = 3;
    rx_state.pending_len = 2;

    lhkt_test_bridge_disconnect_data_socket(&data_fd, &rx_state, &stats);

    assert(data_fd == -1);
    assert(stats.socket_reconnects == 1);
    assert(rx_state.seen_header == 0);
    assert(rx_state.len == 0);
    assert(rx_state.pending_len == 0);

    close(sv[1]);
}

static void test_rx_restore_retry_success_counts_failure(void)
{
    lhkt_config_t cfg;
    lhkt_stats_t stats;
    kiss_params_t params;
    kiss_frame_t frame;
    int config_results[] = { LHKT_OK, LHKT_ERR, LHKT_OK };

    lhkt_config_defaults(&cfg);
    lhkt_stats_init(&stats);
    kiss_params_init(&params);
    make_valid_kiss_frame(&frame);

    lhkt_test_bridge_reset_tx_hooks();
    lhkt_test_bridge_set_config_results(config_results,
                                        sizeof(config_results) / sizeof(config_results[0]));
    lhkt_test_bridge_set_write_result(1);

    assert(lhkt_test_handle_kiss_frame(&frame, &params, &cfg, &stats, 42) == LHKT_OK);

    assert(lhkt_test_bridge_write_call_count() == 1);
    assert(lhkt_test_bridge_config_call_count() == 3);
    assert(lhkt_test_bridge_sleep_call_count() >= 3);
    assert(stats.tx_restore_failures == 1);
    assert(stats.loraham_drop == 0);
    assert(stats.loraham_tx == 1);
}

static void test_rx_restore_retry_failure_is_counted(void)
{
    lhkt_config_t cfg;
    lhkt_stats_t stats;
    kiss_params_t params;
    kiss_frame_t frame;
    int config_results[] = { LHKT_OK, LHKT_ERR, LHKT_ERR };

    lhkt_config_defaults(&cfg);
    lhkt_stats_init(&stats);
    kiss_params_init(&params);
    make_valid_kiss_frame(&frame);

    lhkt_test_bridge_reset_tx_hooks();
    lhkt_test_bridge_set_config_results(config_results,
                                        sizeof(config_results) / sizeof(config_results[0]));
    lhkt_test_bridge_set_write_result(1);

    assert(lhkt_test_handle_kiss_frame(&frame, &params, &cfg, &stats, 42) == LHKT_ERR);

    assert(lhkt_test_bridge_write_call_count() == 1);
    assert(lhkt_test_bridge_config_call_count() == 3);
    assert(stats.tx_restore_failures == 2);
    assert(stats.loraham_drop == 0);
    assert(stats.loraham_tx == 0);
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);

    test_fd_set_rejects_too_large_fd();
    test_shutdown_stop_flag();
    test_stop_aware_wait_helpers();
    test_tnc2_to_kiss_output();
    test_nonzero_kiss_port_is_dropped();
    test_invalid_tnc2_is_dropped();
    test_client_write_failure_returns_socket_error();
    test_tx_write_failure_restores_rx();
    test_shutdown_during_tx_settle_restores_rx();
    test_tx_socket_error_invalidates_data_socket();
    test_rx_restore_retry_success_counts_failure();
    test_rx_restore_retry_failure_is_counted();

    puts("test_bridge: OK");
    return 0;
}
