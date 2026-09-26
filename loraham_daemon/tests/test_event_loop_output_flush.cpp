#include "../client_slot.h"
#include "../event_loop.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

/*
 * EPOLLOUT / queued output flush tests (ClientSlot API).
 *
 * This locks the non-blocking write-completion step: clients with pending
 * output are registered for write readiness (mirroring the daemon's fd
 * reconciliation) and flushed only when ready.
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

static void close_pair(int sv[2])
{
    if (sv[0] >= 0)
        close(sv[0]);
    if (sv[1] >= 0)
        close(sv[1]);
}

/* Mirror of the daemon's reconciliation rule: slots with pending output get
 * READ|WRITE interest, others READ only. */
static void add_slot_to_event_loop(ClientSlot *slot, EventLoopSet *set)
{
    if (!client_slot_has_client(slot))
        return;

    if (client_output_queue_pending(&slot->output) > 0)
        event_loop_add_fd_events(set, client_slot_fd(slot),
                                 EVENT_LOOP_EVENT_READ |
                                 EVENT_LOOP_EVENT_WRITE);
    else
        event_loop_add_fd(set, client_slot_fd(slot));
}

static void test_event_loop_write_ready(void)
{
    int sv[2] = {-1, -1};
    EventLoopSet set;
    EventLoopReadySet ready;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        g_fail++;
        printf("[FAIL] write ready socketpair\n");
        return;
    }

    if (event_loop_init(&set) != 0) {
        close_pair(sv);
        g_fail++;
        printf("[FAIL] write ready event loop init\n");
        return;
    }

    event_loop_add_fd_events(&set, sv[1], EVENT_LOOP_EVENT_READ | EVENT_LOOP_EVENT_WRITE);
    expect_int("write ready wait", event_loop_wait(&set, &ready, 100000) > 0, 1);
    expect_int("write ready flag", event_loop_ready_fd_write(&ready, sv[1]), 1);

    event_loop_close(&set);
    close_pair(sv);
}

static void test_pending_output_registers_write_interest(void)
{
    int sv[2] = {-1, -1};
    ClientSlot slots[1];
    EventLoopSet set;
    EventLoopReadySet ready;
    const uint8_t payload[] = {'O', 'K'};

    client_slot_init_all(slots, 1);

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        g_fail++;
        printf("[FAIL] pending write socketpair\n");
        return;
    }

    client_slot_set_fd(&slots[0], sv[1]);
    client_slot_set_nonblocking(client_slot_fd(&slots[0]));
    expect_int("pending write append",
               client_output_queue_append(&slots[0].output, payload,
                                          sizeof(payload)), 1);

    if (event_loop_init(&set) != 0) {
        client_slot_close(&slots[0]);
        close(sv[0]);
        g_fail++;
        printf("[FAIL] pending write event loop init\n");
        return;
    }

    add_slot_to_event_loop(&slots[0], &set);
    expect_int("pending write wait", event_loop_wait(&set, &ready, 100000) > 0, 1);
    expect_int("pending output ready",
               client_slot_output_ready(&slots[0], &ready), 1);

    event_loop_close(&set);
    close(sv[0]);
    client_slot_close(&slots[0]);
}

static void test_flush_ready_output_sends_payload(void)
{
    int sv[2] = {-1, -1};
    ClientSlot slots[1];
    EventLoopSet set;
    EventLoopReadySet ready;
    const uint8_t payload[] = {'h', 'i'};
    uint8_t got[sizeof(payload)] = {0};

    client_slot_init_all(slots, 1);

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        g_fail++;
        printf("[FAIL] flush ready socketpair\n");
        return;
    }

    client_slot_set_fd(&slots[0], sv[1]);
    client_slot_set_nonblocking(client_slot_fd(&slots[0]));
    expect_int("flush append",
               client_output_queue_append(&slots[0].output, payload,
                                          sizeof(payload)), 1);

    if (event_loop_init(&set) != 0) {
        close(sv[0]);
        client_slot_close(&slots[0]);
        g_fail++;
        printf("[FAIL] flush event loop init\n");
        return;
    }

    add_slot_to_event_loop(&slots[0], &set);
    expect_int("flush wait", event_loop_wait(&set, &ready, 100000) > 0, 1);

    client_slot_flush_ready_outputs(slots, 1, &ready);

    expect_size("flush queue empty",
                client_output_queue_pending(&slots[0].output), 0);
    expect_int("flush read size", (int)read(sv[0], got, sizeof(got)), (int)sizeof(payload));
    expect_int("flush payload", memcmp(got, payload, sizeof(payload)) == 0, 1);

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

    test_event_loop_write_ready();
    test_pending_output_registers_write_interest();
    test_flush_ready_output_sends_payload();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
