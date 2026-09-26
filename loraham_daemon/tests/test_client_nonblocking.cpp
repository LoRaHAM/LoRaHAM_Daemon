#include "../client_slot.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

/*
 * Non-blocking client socket tests (ClientSlot API).
 *
 * Accepted client sockets are non-blocking, EAGAIN does not close slots, and
 * peer EOF is observable as read()==0 (the dispatchers close on that oracle).
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

static int fd_is_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags < 0)
        return 0;

    return (flags & O_NONBLOCK) != 0;
}

static void test_set_nonblocking(void)
{
    int sv[2];

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        g_fail++;
        printf("[FAIL] set nonblocking socketpair\n");
        return;
    }

    expect_int("set nonblocking result", client_slot_set_nonblocking(sv[1]), 0);
    expect_int("set nonblocking flag", fd_is_nonblocking(sv[1]), 1);

    close(sv[0]);
    close(sv[1]);
}

static void test_slot_init_marks_empty(void)
{
    ClientSlot slots[3];

    client_slot_init_all(slots, 3);

    expect_int("slot init empty 0", client_slot_fd(&slots[0]), -1);
    expect_int("slot init empty 1", client_slot_fd(&slots[1]), -1);
    expect_int("slot init empty 2", client_slot_fd(&slots[2]), -1);
    expect_int("slots have no clients", client_slot_has_clients(slots, 3), 0);
}

static void test_slot_add_accepts_fd_zero(void)
{
    ClientSlot slots[1];

    client_slot_init_all(slots, 1);

    expect_int("slot add fd0", client_slot_add(slots, 1, 0), 1);
    expect_int("slot stores fd0", client_slot_fd(&slots[0]), 0);
    expect_int("slot fd0 is client", client_slot_has_clients(slots, 1), 1);

    slots[0].fd = -1;  // Do not close stdin in the unit test.
}

static void test_client_slot_accepts_fd_zero(void)
{
    ClientSlot slot;

    client_slot_init(&slot);

    expect_int("client_slot null fd", client_slot_fd(NULL), -1);
    expect_int("client_slot empty fd", client_slot_fd(&slot), -1);
    expect_int("client_slot empty state", client_slot_has_client(&slot), 0);

    client_slot_set_fd(&slot, 0);

    expect_int("client_slot fd0 stored", client_slot_fd(&slot), 0);
    expect_int("client_slot fd0 is client", client_slot_has_client(&slot), 1);

    slot.fd = -1;  // Do not close stdin in the unit test.
}

static void test_read_eagain_keeps_slot(void)
{
    int sv[2];
    ClientSlot slots[1];
    uint8_t buf[8];
    ssize_t n;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        g_fail++;
        printf("[FAIL] read eagain socketpair\n");
        return;
    }

    client_slot_init_all(slots, 1);
    client_slot_set_fd(&slots[0], sv[1]);
    client_slot_set_nonblocking(client_slot_fd(&slots[0]));

    errno = 0;
    n = read(client_slot_fd(&slots[0]), buf, sizeof(buf));

    expect_int("read eagain result", n < 0, 1);
    expect_int("read eagain errno", errno == EAGAIN || errno == EWOULDBLOCK, 1);
    expect_int("read eagain keeps slot", client_slot_has_client(&slots[0]), 1);

    close(sv[0]);
    client_slot_close(&slots[0]);
}

static void test_peer_close_reads_zero(void)
{
    int sv[2];
    ClientSlot slots[1];
    uint8_t buf[8];

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        g_fail++;
        printf("[FAIL] read close socketpair\n");
        return;
    }

    client_slot_init_all(slots, 1);
    client_slot_set_fd(&slots[0], sv[1]);
    close(sv[0]);

    // The dispatchers close the slot on this read()==0 oracle.
    expect_int("read close returns zero",
               (int)read(client_slot_fd(&slots[0]), buf, sizeof(buf)), 0);
    client_slot_close(&slots[0]);
    expect_int("read close clears slot", client_slot_fd(&slots[0]), -1);
}

static void test_accept_sets_nonblocking_and_emfile(void)
{
    int listen_fd;
    int peer_fd;
    int peer2_fd = -1;
    int accepted_fd;
    ClientSlot slots[1];
    struct sockaddr_un addr;
    char path[sizeof(addr.sun_path)];

    client_slot_init_all(slots, 1);

    snprintf(path, sizeof(path), "/tmp/loraham-nonblock-%ld.sock", (long)getpid());
    unlink(path);

    listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    peer_fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (listen_fd < 0 || peer_fd < 0) {
        g_fail++;
        printf("[FAIL] accept setup sockets\n");
        if (listen_fd >= 0)
            close(listen_fd);
        if (peer_fd >= 0)
            close(peer_fd);
        return;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(listen_fd, 2) != 0 ||
        connect(peer_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        g_fail++;
        printf("[FAIL] accept setup bind/listen/connect\n");
        close(listen_fd);
        close(peer_fd);
        unlink(path);
        return;
    }

    accepted_fd = client_slot_accept_with_output(listen_fd, slots, 1);

    expect_int("accept returns fd", accepted_fd >= 0, 1);
    expect_int("accept stores fd", client_slot_fd(&slots[0]), accepted_fd);
    expect_int("accept fd nonblocking", fd_is_nonblocking(accepted_fd), 1);

    // Slots full: the next accept must fail with EMFILE and close the fd.
    peer2_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (peer2_fd >= 0 &&
        connect(peer2_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        errno = 0;
        expect_int("accept full slots fails",
                   client_slot_accept_with_output(listen_fd, slots, 1), -1);
        expect_int("accept full slots errno", errno, EMFILE);
        expect_int("accept full keeps first client",
                   client_slot_fd(&slots[0]), accepted_fd);
    } else {
        g_fail++;
        printf("[FAIL] accept EMFILE setup\n");
    }

    client_slot_close(&slots[0]);
    if (peer2_fd >= 0)
        close(peer2_fd);
    close(peer_fd);
    close(listen_fd);
    unlink(path);
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

    test_set_nonblocking();
    test_slot_init_marks_empty();
    test_slot_add_accepts_fd_zero();
    test_client_slot_accepts_fd_zero();
    test_read_eagain_keeps_slot();
    test_peer_close_reads_zero();
    test_accept_sets_nonblocking_and_emfile();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
