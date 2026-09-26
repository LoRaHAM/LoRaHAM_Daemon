#include "../client_slot.h"

#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

/*
 * Slow-client isolation tests (ClientSlot API).
 *
 * A client with a full kernel send buffer must not block or disturb other
 * clients: its payload stays queued, the fast client receives immediately,
 * and only a client whose user-space queue overflows is closed.
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

    if (client_slot_set_nonblocking(fd) != 0)
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
    ClientSlot slots[2];
    const uint8_t msg[] = {'O', 'K'};
    uint8_t got[sizeof(msg)] = {0};

    client_slot_init_all(slots, 2);

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, slow) != 0 ||
        socketpair(AF_UNIX, SOCK_STREAM, 0, fast) != 0) {
        g_fail++;
        printf("[FAIL] slow/fast socketpair setup\n");
        close_pair(slow);
        close_pair(fast);
        return;
    }

    client_slot_set_fd(&slots[0], slow[1]);
    client_slot_set_fd(&slots[1], fast[1]);
    slow[1] = -1;
    fast[1] = -1;

    expect_int("slow buffer filled",
               fill_send_buffer_until_eagain(client_slot_fd(&slots[0])), 1);
    expect_int("fast client nonblocking",
               client_slot_set_nonblocking(client_slot_fd(&slots[1])), 0);

    client_slot_broadcast_bytes_queued(slots, 2, msg, sizeof(msg));

    expect_int("slow client kept", client_slot_has_client(&slots[0]), 1);
    expect_size_ge("slow client has pending output",
                   client_output_queue_pending(&slots[0].output),
                   sizeof(msg));
    expect_int("fast client kept", client_slot_has_client(&slots[1]), 1);
    expect_int("fast client received", wait_read_exact(fast[0], got, sizeof(got)), 1);
    expect_int("fast client payload", memcmp(got, msg, sizeof(msg)) == 0, 1);

    close_pair(slow);
    close_pair(fast);
    client_slot_close_all(slots, 2);
}

static void test_queue_overflow_closes_only_full_client(void)
{
    int full[2] = {-1, -1};
    int fast[2] = {-1, -1};
    ClientSlot slots[2];
    static uint8_t full_payload[CLIENT_OUTPUT_QUEUE_CAPACITY];
    const uint8_t msg[] = {'!'};
    uint8_t got = 0;

    memset(full_payload, 'F', sizeof(full_payload));
    client_slot_init_all(slots, 2);

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, full) != 0 ||
        socketpair(AF_UNIX, SOCK_STREAM, 0, fast) != 0) {
        g_fail++;
        printf("[FAIL] overflow socketpair setup\n");
        close_pair(full);
        close_pair(fast);
        return;
    }

    client_slot_set_fd(&slots[0], full[1]);
    client_slot_set_fd(&slots[1], fast[1]);
    full[1] = -1;
    fast[1] = -1;

    expect_int("overflow full client nonblocking",
               client_slot_set_nonblocking(client_slot_fd(&slots[0])), 0);
    expect_int("overflow fast client nonblocking",
               client_slot_set_nonblocking(client_slot_fd(&slots[1])), 0);
    expect_int("fill output queue",
               client_output_queue_append(&slots[0].output, full_payload,
                                          sizeof(full_payload)),
               1);

    client_slot_broadcast_bytes_queued(slots, 2, msg, sizeof(msg));

    expect_int("overflow client closed", client_slot_fd(&slots[0]), -1);
    expect_int("overflow queue reset",
               (int)client_output_queue_pending(&slots[0].output), 0);
    expect_int("overflow fast client kept", client_slot_has_client(&slots[1]), 1);
    expect_int("overflow fast client received",
               wait_read_exact(fast[0], &got, sizeof(got)), 1);
    expect_int("overflow fast payload", got == msg[0], 1);

    close_pair(full);
    close_pair(fast);
    client_slot_close_all(slots, 2);
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
