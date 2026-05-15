#include "ax25.h"
#include "kiss.h"
#include "loraham_kiss_tnc.h"
#include "tnc2.h"

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int lhkt_test_send_tnc2_to_kiss_client(int client_fd,
                                       const char *tnc2,
                                       lhkt_stats_t *stats);

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

int main(void)
{
    signal(SIGPIPE, SIG_IGN);

    test_tnc2_to_kiss_output();
    test_invalid_tnc2_is_dropped();

    puts("test_bridge: OK");
    return 0;
}
