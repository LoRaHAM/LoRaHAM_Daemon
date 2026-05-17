#include "../client_set.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Client output queue tests.
 *
 * These helpers prepare the later non-blocking write path. The live
 * broadcast behavior is intentionally unchanged in this step.
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

static void test_init_and_append(void)
{
    ClientOutputQueue q;
    const uint8_t payload[] = {0x00, 0x41, 0xff};

    client_output_queue_init(&q);

    expect_size("init pending", client_output_queue_pending(&q), 0);
    expect_int("append payload", client_output_queue_append(&q, payload, sizeof(payload)), 1);
    expect_size("append pending", client_output_queue_pending(&q), sizeof(payload));
    expect_int("append payload bytes",
               memcmp(client_output_queue_data(&q), payload, sizeof(payload)) == 0,
               1);
}

static void test_consume_keeps_tail(void)
{
    ClientOutputQueue q;
    const uint8_t payload[] = {1, 2, 3, 4, 5};
    const uint8_t expected_tail[] = {3, 4, 5};

    client_output_queue_init(&q);
    expect_int("consume setup append", client_output_queue_append(&q, payload, sizeof(payload)), 1);

    expect_size("consume count", client_output_queue_consume(&q, 2), 2);
    expect_size("consume pending", client_output_queue_pending(&q), sizeof(expected_tail));
    expect_int("consume tail bytes",
               memcmp(client_output_queue_data(&q), expected_tail, sizeof(expected_tail)) == 0,
               1);
}

static void test_consume_more_than_pending_resets(void)
{
    ClientOutputQueue q;
    const uint8_t payload[] = {1, 2, 3};

    client_output_queue_init(&q);
    expect_int("consume all setup append", client_output_queue_append(&q, payload, sizeof(payload)), 1);

    expect_size("consume all count", client_output_queue_consume(&q, 99), sizeof(payload));
    expect_size("consume all pending", client_output_queue_pending(&q), 0);
    expect_int("consume all data null", client_output_queue_data(&q) == NULL, 1);
}

static void test_overflow_rejected(void)
{
    ClientOutputQueue q;
    uint8_t fill[CLIENT_OUTPUT_QUEUE_CAPACITY];
    uint8_t extra = 0xaa;

    memset(fill, 0x55, sizeof(fill));
    client_output_queue_init(&q);

    expect_int("fill queue", client_output_queue_append(&q, fill, sizeof(fill)), 1);
    errno = 0;
    expect_int("reject overflow", client_output_queue_append(&q, &extra, sizeof(extra)), 0);
    expect_int("overflow errno", errno, ENOBUFS);
    expect_size("overflow pending unchanged",
                client_output_queue_pending(&q),
                CLIENT_OUTPUT_QUEUE_CAPACITY);
}

static void test_invalid_args(void)
{
    ClientOutputQueue q;
    const uint8_t payload[] = {1};

    client_output_queue_init(&q);

    errno = 0;
    expect_int("append null queue", client_output_queue_append(NULL, payload, sizeof(payload)), 0);
    expect_int("append null queue errno", errno, EINVAL);

    errno = 0;
    expect_int("append null data", client_output_queue_append(&q, NULL, sizeof(payload)), 0);
    expect_int("append null data errno", errno, EINVAL);

    expect_int("append empty null data", client_output_queue_append(&q, NULL, 0), 1);
    expect_size("append empty unchanged", client_output_queue_pending(&q), 0);
}

static void test_init_all(void)
{
    ClientOutputQueue queues[3];
    const uint8_t payload[] = {1, 2, 3};

    for (int i = 0; i < 3; i++) {
        client_output_queue_init(&queues[i]);
        client_output_queue_append(&queues[i], payload, sizeof(payload));
    }

    client_output_queue_init_all(queues, 3);

    expect_size("init all 0", client_output_queue_pending(&queues[0]), 0);
    expect_size("init all 1", client_output_queue_pending(&queues[1]), 0);
    expect_size("init all 2", client_output_queue_pending(&queues[2]), 0);
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

    test_init_and_append();
    test_consume_keeps_tail();
    test_consume_more_than_pending_resets();
    test_overflow_rejected();
    test_invalid_args();
    test_init_all();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
