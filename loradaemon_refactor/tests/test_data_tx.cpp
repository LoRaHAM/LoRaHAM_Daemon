#include "../data_tx.h"
#include "../client_set.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

/*
 * DATA TX unit tests.
 *
 * This locks the LoRa packet chunk size before DATA TX handling is moved
 * out of the daemon file.
 */

static int g_ok = 0;
static int g_fail = 0;

/* --- Test helpers --- */

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

/* --- Chunking behavior --- */

static void test_chunk_size(void)
{
    expect_size("zero remaining", data_tx_chunk_size(0), 0);
    expect_size("one byte", data_tx_chunk_size(1), 1);
    expect_size("max exact", data_tx_chunk_size(255), 255);
    expect_size("max plus one", data_tx_chunk_size(256), 255);
    expect_size("large input", data_tx_chunk_size(2048), 255);
}


typedef struct {
    int calls;
    size_t sizes[4];
    size_t offsets[4];
} ChunkRecorder;

static int record_chunk(uint8_t *chunk, size_t len, size_t offset, void *ctx)
{
    ChunkRecorder *rec = (ChunkRecorder *)ctx;

    (void)chunk;
    rec->sizes[rec->calls] = len;
    rec->offsets[rec->calls] = offset;
    rec->calls++;
    return 0;
}

static int stop_on_second_chunk(uint8_t *chunk, size_t len, size_t offset, void *ctx)
{
    ChunkRecorder *rec = (ChunkRecorder *)ctx;

    (void)chunk;
    rec->sizes[rec->calls] = len;
    rec->offsets[rec->calls] = offset;
    rec->calls++;

    return rec->calls == 2 ? 1 : 0;
}

static void test_chunk_iterator(void)
{
    uint8_t buf[600] = {0};
    ChunkRecorder rec = {0, {0}, {0}};

    expect_size("iterator all bytes",
                data_tx_for_each_chunk(buf, sizeof(buf), record_chunk, &rec),
                sizeof(buf));
    expect_int("iterator call count", rec.calls, 3);
    expect_size("iterator first size", rec.sizes[0], 255);
    expect_size("iterator second size", rec.sizes[1], 255);
    expect_size("iterator third size", rec.sizes[2], 90);
    expect_size("iterator second offset", rec.offsets[1], 255);
    expect_size("iterator third offset", rec.offsets[2], 510);
}

static void test_chunk_iterator_stop(void)
{
    uint8_t buf[600] = {0};
    ChunkRecorder rec = {0, {0}, {0}};

    expect_size("iterator stop returns sent bytes",
                data_tx_for_each_chunk(buf, sizeof(buf), stop_on_second_chunk, &rec),
                255);
    expect_int("iterator stop call count", rec.calls, 2);
}


/* --- DATA TX dispatch via event loop --- */

static void test_process_clients_backend(const char *name, int use_epoll)
{
    int sv[2];
    int clients[2] = {0};
    EventLoopSet set;
    EventLoopReadySet ready;
    uint8_t payload[300];
    ChunkRecorder rec = {0, {0}, {0}};

    memset(payload, 'A', sizeof(payload));

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        g_fail++;
        printf("[FAIL] %s socketpair\n", name);
        return;
    }

    clients[0] = sv[1];

    if (use_epoll) {
        if (event_loop_init_epoll(&set) != 0) {
            close(sv[0]);
            client_set_close_slot(clients, 0);
            g_fail++;
            printf("[FAIL] %s epoll init\n", name);
            return;
        }
    } else {
        event_loop_init_select(&set);
    }

    if (write(sv[0], payload, sizeof(payload)) != (ssize_t)sizeof(payload)) {
        close(sv[0]);
        client_set_close_slot(clients, 0);
        event_loop_close(&set);
        g_fail++;
        printf("[FAIL] %s write\n", name);
        return;
    }

    event_loop_add_fd(&set, sv[1]);
    expect_int(name, event_loop_wait(&set, &ready, 100000), 1);

    data_tx_process_clients(name, clients, 2, &ready, record_chunk, &rec);

    expect_int("process clients call count", rec.calls, 2);
    expect_size("process clients first chunk", rec.sizes[0], 255);
    expect_size("process clients second chunk", rec.sizes[1], 45);
    expect_size("process clients second offset", rec.offsets[1], 255);

    close(sv[0]);
    client_set_close_slot(clients, 0);
    event_loop_close(&set);
}

static void test_process_clients_select(void)
{
    test_process_clients_backend("process clients select wait", 0);
}

static void test_process_clients_epoll(void)
{
    test_process_clients_backend("process clients epoll wait", 1);
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

    test_chunk_size();
    test_chunk_iterator();
    test_chunk_iterator_stop();
    test_process_clients_epoll();
    test_process_clients_select();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
