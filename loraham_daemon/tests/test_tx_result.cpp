#include "../tx_result.h"

#include <stdio.h>
#include <string.h>

/* --- TX result helper tests --- */

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

static void expect_str(const char *name, const char *actual, const char *expected)
{
    if (strcmp(actual, expected) == 0) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: expected %s, got %s\n", name, expected, actual);
    }
}

static void test_tx_result_names(void)
{
    expect_str("tx ok name", tx_result_name(TX_RESULT_OK), "OK");
    expect_str("tx invalid band name", tx_result_name(TX_RESULT_INVALID_BAND), "INVALID_BAND");
    expect_str("tx invalid packet name", tx_result_name(TX_RESULT_INVALID_PACKET), "INVALID_PACKET");
    expect_str("tx busy name", tx_result_name(TX_RESULT_BUSY), "BUSY");
    expect_str("tx cad timeout name", tx_result_name(TX_RESULT_CAD_TIMEOUT), "CAD_TIMEOUT");
    expect_str("tx radio not ready name", tx_result_name(TX_RESULT_RADIO_NOT_READY), "RADIO_NOT_READY");
    expect_str("tx radio error name", tx_result_name(TX_RESULT_RADIO_ERROR), "RADIO_ERROR");
}

static void test_tx_result_ok(void)
{
    expect_int("tx ok is ok", tx_result_is_ok(TX_RESULT_OK), 1);
    expect_int("tx busy is not ok", tx_result_is_ok(TX_RESULT_BUSY), 0);
    expect_int("tx radio not ready is not ok", tx_result_is_ok(TX_RESULT_RADIO_NOT_READY), 0);
    expect_int("tx radio error is not ok", tx_result_is_ok(TX_RESULT_RADIO_ERROR), 0);
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

    test_tx_result_names();
    test_tx_result_ok();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
