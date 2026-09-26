#include "../client_slot.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

/*
 * Queued broadcast tests (ClientSlot API).
 *
 * These tests lock the non-blocking write path used by the daemon before
 * EPOLLOUT-based background flushing kicks in.
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
    ClientSlot slots[2];
    const uint8_t payload[] = {0x00, 0x41, 0xff};
    uint8_t got[sizeof(payload)] = {0};

    client_slot_init_all(slots, 2);

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        g_fail++;
        printf("[FAIL] queued broadcast socketpair setup\n");
        return;
    }

    client_slot_set_fd(&slots[0], sv[1]);
    client_slot_set_nonblocking(client_slot_fd(&slots[0]));

    client_slot_broadcast_bytes_queued(slots, 2, payload, sizeof(payload));

    expect_int("queued broadcast slot kept", client_slot_has_client(&slots[0]), 1);
    expect_size("queued broadcast pending empty",
                client_output_queue_pending(&slots[0].output), 0);
    expect_int("queued broadcast read size",
               (int)read(sv[0], got, sizeof(got)),
               (int)sizeof(payload));
    expect_int("queued broadcast payload",
               memcmp(got, payload, sizeof(payload)) == 0,
               1);

    close(sv[0]);
    client_slot_close(&slots[0]);
}

static void test_queue_overflow_closes_client(void)
{
    int sv[2];
    ClientSlot slots[1];
    uint8_t one = 0x42;

    client_slot_init_all(slots, 1);

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        g_fail++;
        printf("[FAIL] overflow socketpair setup\n");
        return;
    }

    client_slot_set_fd(&slots[0], sv[1]);
    client_slot_set_nonblocking(client_slot_fd(&slots[0]));

    slots[0].output.len = CLIENT_OUTPUT_QUEUE_CAPACITY;

    client_slot_broadcast_bytes_queued(slots, 1, &one, 1);

    expect_int("overflow closes slot", client_slot_fd(&slots[0]), -1);
    expect_size("overflow resets queue",
                client_output_queue_pending(&slots[0].output), 0);

    close(sv[0]);
}

static void test_close_resets_reused_slot(void)
{
    ClientSlot slots[1];

    client_slot_init_all(slots, 1);
    slots[0].output.len = 123;

    expect_int("close reset setup has pending",
               client_output_queue_pending(&slots[0].output) > 0, 1);

    /* Reset behavior is exercised through close in unit scope;
       radio_channel_accept_ready uses client_slot_accept_with_output in
       daemon scope (fd reuse gets a fresh queue via client_slot_set_fd). */
    client_slot_close(&slots[0]);
    expect_size("close resets queue",
                client_output_queue_pending(&slots[0].output), 0);
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
    test_close_resets_reused_slot();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
