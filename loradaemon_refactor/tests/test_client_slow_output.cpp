#include "../client_set.h"

#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/*
 * Slow client output tests.
 *
 * These tests lock the important non-blocking broadcast behavior:
 * a slow client must not block delivery to a fast client, and a full
 * per-client output queue closes only that client.
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

static void expect_size_ge(const char *name, size_t actual, size_t expected_min)
{
    if (actual >= expected_min) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: expected >= %zu, got %zu\n", name, expected_min, actual);
    }
}

static void close_if_open(int *fd)
{
    if (*fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

static void close_pair(int sv[2])
{
    close_if_open(&sv[0]);
    close_if_open(&sv[1]);
}

static int wait_read_exact(int fd, uint8_t *buf, size_t len)
{
    struct pollfd pfd;
    size_t got = 0;

    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    while (got < len) {
        int rc = poll(&pfd, 1, 1000);
        if (rc <= 0)
            return 0;

        if (pfd.revents & (POLLERR | POLLNVAL))
            return 0;

        if (pfd.revents & POLLIN) {
            ssize_t n = read(fd, buf + got, len - got);
            if (n <= 0)
                return 0;
            got += (size_t)n;
        }
    }

    return 1;
}

static int fill_send_buffer_until_eagain(int fd)
{
    uint8_t payload[1024];
    int sndbuf = 4096;

    memset(payload, 'S', sizeof(payload));
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    if (client_set_set_nonblocking(fd) != 0)
        return 0;

    for (int i = 0; i < 20000; i++) {
        ssize_t n = send(fd, payload, sizeof(payload), MSG_NOSIGNAL);

        if (n > 0)
            continue;

        if (n < 0 && errno == EINTR) {
            i--;
            continue;
        }

        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return 1;

        return 0;
    }

    return 0;
}

static void test_slow_client_does_not_block_fast_client(void)
{
    int slow[2] = {-1, -1};
    int fast[2] = {-1, -1};
    int clients[2] = {0};
    ClientOutputQueue queues[2];
    const uint8_t msg[] = {'O', 'K'};
    uint8_t got[sizeof(msg)] = {0};

    client_output_queue_init_all(queues, 2);

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, slow) != 0 ||
        socketpair(AF_UNIX, SOCK_STREAM, 0, fast) != 0) {
        g_fail++;
        printf("[FAIL] slow/fast socketpair setup\n");
        close_pair(slow);
        close_pair(fast);
        return;
    }

    clients[0] = slow[1];
    clients[1] = fast[1];
    slow[1] = -1;
    fast[1] = -1;

    expect_int("slow buffer filled", fill_send_buffer_until_eagain(clients[0]), 1);
    expect_int("fast client nonblocking", client_set_set_nonblocking(clients[1]), 0);

    client_set_broadcast_bytes_queued(clients, queues, 2, msg, sizeof(msg));

    expect_int("slow client kept", clients[0] > 0, 1);
    expect_size_ge("slow client has pending output",
                   client_output_queue_pending(&queues[0]),
                   sizeof(msg));
    expect_int("fast client kept", clients[1] > 0, 1);
    expect_int("fast client received", wait_read_exact(fast[0], got, sizeof(got)), 1);
    expect_int("fast client payload", memcmp(got, msg, sizeof(msg)) == 0, 1);

    close_pair(slow);
    close_pair(fast);
    client_set_close_all_with_output(clients, queues, 2);
}

static void test_queue_overflow_closes_only_full_client(void)
{
    int full[2] = {-1, -1};
    int fast[2] = {-1, -1};
    int clients[2] = {0};
    ClientOutputQueue queues[2];
    static uint8_t full_payload[CLIENT_OUTPUT_QUEUE_CAPACITY];
    const uint8_t msg[] = {'!'};
    uint8_t got = 0;

    memset(full_payload, 'F', sizeof(full_payload));
    client_output_queue_init_all(queues, 2);

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, full) != 0 ||
        socketpair(AF_UNIX, SOCK_STREAM, 0, fast) != 0) {
        g_fail++;
        printf("[FAIL] overflow socketpair setup\n");
        close_pair(full);
        close_pair(fast);
        return;
    }

    clients[0] = full[1];
    clients[1] = fast[1];
    full[1] = -1;
    fast[1] = -1;

    expect_int("overflow full client nonblocking", client_set_set_nonblocking(clients[0]), 0);
    expect_int("overflow fast client nonblocking", client_set_set_nonblocking(clients[1]), 0);
    expect_int("fill output queue",
               client_output_queue_append(&queues[0], full_payload, sizeof(full_payload)),
               1);

    client_set_broadcast_bytes_queued(clients, queues, 2, msg, sizeof(msg));

    expect_int("overflow client closed", clients[0], 0);
    expect_int("overflow queue reset", (int)client_output_queue_pending(&queues[0]), 0);
    expect_int("overflow fast client kept", clients[1] > 0, 1);
    expect_int("overflow fast client received", wait_read_exact(fast[0], &got, sizeof(got)), 1);
    expect_int("overflow fast payload", got == msg[0], 1);

    close_pair(full);
    close_pair(fast);
    client_set_close_all_with_output(clients, queues, 2);
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

    test_slow_client_does_not_block_fast_client();
    test_queue_overflow_closes_only_full_client();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
