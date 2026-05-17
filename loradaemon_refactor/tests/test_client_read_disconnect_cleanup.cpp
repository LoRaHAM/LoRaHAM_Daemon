#include "../client_set.h"
#include "../data_tx.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

/*
 * Read-side disconnect cleanup tests.
 *
 * These lock the queue cleanup after non-blocking writes were introduced:
 * closing a client from the read path must reset its queued output too.
 */

static int g_ok = 0;
static int g_fail = 0;

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

static void test_read_eof_resets_output_queue(void)
{
    int sv[2];
    int clients[1] = {0};
    ClientOutputQueue queues[1];
    char buf[8];
    const uint8_t pending[] = {'x', 'y', 'z'};

    client_output_queue_init_all(queues, 1);

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        g_fail++;
        printf("[FAIL] read eof socketpair\n");
        return;
    }

    clients[0] = sv[1];
    expect_int("read eof append", client_output_queue_append(&queues[0], pending, sizeof(pending)), 1);
    expect_size("read eof pending before", client_output_queue_pending(&queues[0]), sizeof(pending));

    close(sv[0]);

    expect_int("read eof result", (int)client_set_read_slot_with_output(clients, queues, 0, buf, sizeof(buf)), 0);
    expect_int("read eof client closed", clients[0], 0);
    expect_size("read eof queue reset", client_output_queue_pending(&queues[0]), 0);
}

static void test_read_eagain_keeps_output_queue(void)
{
    int sv[2];
    int clients[1] = {0};
    ClientOutputQueue queues[1];
    char buf[8];
    const uint8_t pending[] = {'a', 'b'};

    client_output_queue_init_all(queues, 1);

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        g_fail++;
        printf("[FAIL] read eagain socketpair\n");
        return;
    }

    clients[0] = sv[1];
    client_set_set_nonblocking(clients[0]);
    expect_int("read eagain append", client_output_queue_append(&queues[0], pending, sizeof(pending)), 1);

    errno = 0;
    expect_int("read eagain result", (int)client_set_read_slot_with_output(clients, queues, 0, buf, sizeof(buf)), -1);
    expect_int("read eagain errno", errno == EAGAIN || errno == EWOULDBLOCK, 1);
    expect_int("read eagain client kept", clients[0] > 0, 1);
    expect_size("read eagain queue kept", client_output_queue_pending(&queues[0]), sizeof(pending));

    close(sv[0]);
    client_set_close_slot_with_output(clients, queues, 0);
}

typedef struct {
    int calls;
} ChunkCounter;

static int count_chunk(uint8_t *chunk, size_t len, size_t offset, void *ctx)
{
    ChunkCounter *counter = (ChunkCounter *)ctx;

    (void)chunk;
    (void)len;
    (void)offset;
    counter->calls++;

    return 0;
}

static void test_data_tx_eof_resets_output_queue(void)
{
    int sv[2];
    int clients[1] = {0};
    ClientOutputQueue queues[1];
    EventLoopSet set;
    EventLoopReadySet ready;
    ChunkCounter counter = {0};
    const uint8_t pending[] = {'q'};

    client_output_queue_init_all(queues, 1);

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        g_fail++;
        printf("[FAIL] data tx eof socketpair\n");
        return;
    }

    clients[0] = sv[1];
    expect_int("data tx eof append", client_output_queue_append(&queues[0], pending, sizeof(pending)), 1);
    close(sv[0]);

    if (event_loop_init(&set) != 0) {
        client_set_close_slot_with_output(clients, queues, 0);
        g_fail++;
        printf("[FAIL] data tx eof event loop init\n");
        return;
    }

    event_loop_add_fd(&set, clients[0]);
    expect_int("data tx eof wait", event_loop_wait(&set, &ready, 100000), 1);

    data_tx_process_clients_with_output("TEST", clients, queues, 1, &ready, count_chunk, &counter);

    expect_int("data tx eof no chunks", counter.calls, 0);
    expect_int("data tx eof client closed", clients[0], 0);
    expect_size("data tx eof queue reset", client_output_queue_pending(&queues[0]), 0);

    event_loop_close(&set);
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

    test_read_eof_resets_output_queue();
    test_read_eagain_keeps_output_queue();
    test_data_tx_eof_resets_output_queue();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
