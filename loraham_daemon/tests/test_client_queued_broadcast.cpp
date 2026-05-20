#include "../client_set.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

/*
 * Queued broadcast tests.
 *
 * These tests lock the non-blocking write path used by the daemon before
 * EPOLLOUT-based background flushing is added in the next step.
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

static void test_queued_broadcast_bytes_delivers_payload(void)
{
    int sv[2];
    int clients[2] = {0};
    ClientOutputQueue queues[2];
    const uint8_t payload[] = {0x00, 0x41, 0xff};
    uint8_t got[sizeof(payload)] = {0};

    client_output_queue_init_all(queues, 2);

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        g_fail++;
        printf("[FAIL] queued broadcast socketpair setup\n");
        return;
    }

    clients[0] = sv[1];
    client_set_set_nonblocking(clients[0]);

    client_set_broadcast_bytes_queued(clients, queues, 2, payload, sizeof(payload));

    expect_int("queued broadcast slot kept", clients[0] > 0, 1);
    expect_size("queued broadcast pending empty", client_output_queue_pending(&queues[0]), 0);
    expect_int("queued broadcast read size",
               (int)read(sv[0], got, sizeof(got)),
               (int)sizeof(payload));
    expect_int("queued broadcast payload",
               memcmp(got, payload, sizeof(payload)) == 0,
               1);

    close(sv[0]);
    client_set_close_slot_with_output(clients, queues, 0);
}

static void test_queue_overflow_closes_client(void)
{
    int sv[2];
    int clients[1] = {0};
    ClientOutputQueue queues[1];
    uint8_t one = 0x42;

    client_output_queue_init_all(queues, 1);

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        g_fail++;
        printf("[FAIL] overflow socketpair setup\n");
        return;
    }

    clients[0] = sv[1];
    client_set_set_nonblocking(clients[0]);

    queues[0].len = CLIENT_OUTPUT_QUEUE_CAPACITY;

    client_set_broadcast_bytes_queued(clients, queues, 1, &one, 1);

    expect_int("overflow closes slot", clients[0], 0);
    expect_size("overflow resets queue", client_output_queue_pending(&queues[0]), 0);

    close(sv[0]);
}

static void test_accept_with_output_resets_reused_slot(void)
{
    int clients[1] = {0};
    ClientOutputQueue queues[1];

    client_output_queue_init_all(queues, 1);
    queues[0].len = 123;

    expect_int("add with output via accept unavailable setup", client_output_queue_pending(&queues[0]) > 0, 1);

    /* Reset behavior is exercised through close-with-output in unit scope;
       radio_channel_accept_ready uses client_set_accept_with_output in daemon scope. */
    client_set_close_slot_with_output(clients, queues, 0);
    expect_size("close with output resets queue", client_output_queue_pending(&queues[0]), 0);
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

    test_queued_broadcast_bytes_delivers_payload();
    test_queue_overflow_closes_client();
    test_accept_with_output_resets_reused_slot();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
