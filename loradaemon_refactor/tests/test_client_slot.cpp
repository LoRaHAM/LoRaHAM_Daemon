#include "../client_slot.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* --- ClientSlot tests ---------------------------------------------------- */

static int g_ok = 0;
static int g_fail = 0;

static void expect_int(const char *name, int actual, int expected)
{
    if (actual == expected) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: expected %d, got %d\n",
               name, expected, actual);
    }
}

static void expect_size(const char *name, size_t actual, size_t expected)
{
    if (actual == expected) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: expected %zu, got %zu\n",
               name, expected, actual);
    }
}

static void test_init_resets_all_state(void)
{
    ClientSlot slot;

    slot.fd = 123;
    slot.output.len = 42;
    slot.stream.len = 7;

    client_slot_init(&slot);

    expect_int("init fd", slot.fd, 0);
    expect_size("init output", client_output_queue_pending(&slot.output), 0);
    expect_size("init stream", slot.stream.len, 0);
}

static void test_init_all_resets_all_slots(void)
{
    ClientSlot slots[3];

    for (int i = 0; i < 3; i++) {
        slots[i].fd = i + 10;
        slots[i].output.len = (size_t)(i + 1);
        slots[i].stream.len = (size_t)(i + 2);
    }

    client_slot_init_all(slots, 3);

    for (int i = 0; i < 3; i++) {
        expect_int("init_all fd", slots[i].fd, 0);
        expect_size("init_all output", client_output_queue_pending(&slots[i].output), 0);
        expect_size("init_all stream", slots[i].stream.len, 0);
    }
}

static void test_set_fd_resets_dependent_state(void)
{
    ClientSlot slot;

    client_slot_init(&slot);

    client_output_queue_append(&slot.output, (const uint8_t *)"abc", 3);
    slot.stream.len = 9;

    client_slot_set_fd(&slot, 55);

    expect_int("set fd", client_slot_fd(&slot), 55);
    expect_int("set fd has client", client_slot_has_client(&slot), 1);
    expect_size("set fd resets output", client_output_queue_pending(&slot.output), 0);
    expect_size("set fd resets stream", slot.stream.len, 0);
}

static void test_close_resets_fd_output_and_stream(void)
{
    ClientSlot slot;
    int sv[2];

    client_slot_init(&slot);

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        g_fail++;
        printf("[FAIL] close socketpair\n");
        return;
    }

    client_slot_set_fd(&slot, sv[0]);
    client_output_queue_append(&slot.output, (const uint8_t *)"abc", 3);
    slot.stream.len = 5;

    client_slot_close(&slot);

    expect_int("close fd", slot.fd, 0);
    expect_int("close has no client", client_slot_has_client(&slot), 0);
    expect_size("close resets output", client_output_queue_pending(&slot.output), 0);
    expect_size("close resets stream", slot.stream.len, 0);

    close(sv[1]);
}

static void test_close_all_resets_multiple_slots(void)
{
    ClientSlot slots[2];
    int sv0[2];
    int sv1[2];

    client_slot_init_all(slots, 2);

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv0) != 0 ||
        socketpair(AF_UNIX, SOCK_STREAM, 0, sv1) != 0) {
        g_fail++;
        printf("[FAIL] close_all socketpair\n");
        return;
    }

    client_slot_set_fd(&slots[0], sv0[0]);
    client_slot_set_fd(&slots[1], sv1[0]);
    client_output_queue_append(&slots[0].output, (const uint8_t *)"x", 1);
    client_output_queue_append(&slots[1].output, (const uint8_t *)"y", 1);
    slots[0].stream.len = 1;
    slots[1].stream.len = 2;

    client_slot_close_all(slots, 2);

    expect_int("close_all slot0 fd", slots[0].fd, 0);
    expect_int("close_all slot1 fd", slots[1].fd, 0);
    expect_size("close_all slot0 output", client_output_queue_pending(&slots[0].output), 0);
    expect_size("close_all slot1 output", client_output_queue_pending(&slots[1].output), 0);
    expect_size("close_all slot0 stream", slots[0].stream.len, 0);
    expect_size("close_all slot1 stream", slots[1].stream.len, 0);

    close(sv0[1]);
    close(sv1[1]);
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

    test_init_resets_all_state();
    test_init_all_resets_all_slots();
    test_set_fd_resets_dependent_state();
    test_close_resets_fd_output_and_stream();
    test_close_all_resets_multiple_slots();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
