#include "../client_set.h"
#include "../event_loop.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

/*
 * EPOLLOUT / queued output flush tests.
 *
 * This locks the non-blocking write-completion step: clients with pending
 * output are registered for write readiness and flushed only when ready.
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
    int clients[1] = {0};
    ClientOutputQueue queues[1];
    EventLoopSet set;
    EventLoopReadySet ready;
    const uint8_t payload[] = {'O', 'K'};

    client_output_queue_init_all(queues, 1);

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        g_fail++;
        printf("[FAIL] pending write socketpair\n");
        return;
    }

    clients[0] = sv[1];
    client_set_set_nonblocking(clients[0]);
    expect_int("pending write append", client_output_queue_append(&queues[0], payload, sizeof(payload)), 1);

    if (event_loop_init(&set) != 0) {
        client_set_close_slot_with_output(clients, queues, 0);
        close(sv[0]);
        g_fail++;
        printf("[FAIL] pending write event loop init\n");
        return;
    }

    client_set_add_to_event_loop_with_output(clients, queues, 1, &set);
    expect_int("pending write wait", event_loop_wait(&set, &ready, 100000) > 0, 1);
    expect_int("pending output ready", client_set_output_ready(clients, queues, 0, &ready), 1);

    event_loop_close(&set);
    close(sv[0]);
    client_set_close_slot_with_output(clients, queues, 0);
}

static void test_flush_ready_output_sends_payload(void)
{
    int sv[2] = {-1, -1};
    int clients[1] = {0};
    ClientOutputQueue queues[1];
    EventLoopSet set;
    EventLoopReadySet ready;
    const uint8_t payload[] = {'h', 'i'};
    uint8_t got[sizeof(payload)] = {0};

    client_output_queue_init_all(queues, 1);

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        g_fail++;
        printf("[FAIL] flush ready socketpair\n");
        return;
    }

    clients[0] = sv[1];
    client_set_set_nonblocking(clients[0]);
    expect_int("flush append", client_output_queue_append(&queues[0], payload, sizeof(payload)), 1);

    if (event_loop_init(&set) != 0) {
        close(sv[0]);
        client_set_close_slot_with_output(clients, queues, 0);
        g_fail++;
        printf("[FAIL] flush event loop init\n");
        return;
    }

    client_set_add_to_event_loop_with_output(clients, queues, 1, &set);
    expect_int("flush wait", event_loop_wait(&set, &ready, 100000) > 0, 1);

    client_set_flush_ready_outputs(clients, queues, 1, &ready);

    expect_size("flush queue empty", client_output_queue_pending(&queues[0]), 0);
    expect_int("flush read size", (int)read(sv[0], got, sizeof(got)), (int)sizeof(payload));
    expect_int("flush payload", memcmp(got, payload, sizeof(payload)) == 0, 1);

    event_loop_close(&set);
    close(sv[0]);
    client_set_close_slot_with_output(clients, queues, 0);
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
