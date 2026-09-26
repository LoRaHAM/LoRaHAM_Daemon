#include "../daemon_stats.h"

#include <stdint.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <thread>

/* --- Runtime statistics tests --- */

static int g_ok = 0;
static int g_fail = 0;

static void expect_ulong(const char *name, unsigned long actual, unsigned long expected)
{
    if (actual == expected) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: expected %lu, got %lu\n", name, expected, actual);
    }
}

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

static void expect_long(const char *name, long actual, long expected)
{
    if (actual == expected) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: expected %ld, got %ld\n", name, expected, actual);
    }
}

static void test_tx_result_counters(void)
{
    DaemonRadioStats stats;

    daemon_radio_stats_init(&stats);

    daemon_radio_stats_record_tx_result(&stats, TX_RESULT_OK);
    daemon_radio_stats_record_tx_result(&stats, TX_RESULT_BUSY);
    daemon_radio_stats_record_tx_result(&stats, TX_RESULT_RADIO_ERROR);

    expect_ulong("tx ok once", stats.tx_ok, 1);
    expect_ulong("tx busy once", stats.tx_busy, 1);
    expect_ulong("tx error once", stats.tx_errors, 1);
    expect_ulong("cad untouched", stats.cad_timeouts, 0);
}

static void test_cad_timeout_counted_once_via_tx_result(void)
{
    DaemonRadioStats stats;

    daemon_radio_stats_init(&stats);

    daemon_radio_stats_record_tx_result(&stats, TX_RESULT_CAD_TIMEOUT);

    expect_ulong("cad timeout once", stats.cad_timeouts, 1);
    expect_ulong("cad timeout not tx error", stats.tx_errors, 0);
    expect_ulong("cad timeout not busy", stats.tx_busy, 0);
}

static void test_stats_format_saturated_counters_fit(void)
{
    DaemonRadioStats stats;
    char buf[512];

    daemon_radio_stats_init(&stats);
    stats.rx_packets = ULONG_MAX;
    stats.rx_bytes = ULONG_MAX;
    stats.rx_drops = ULONG_MAX;
    stats.tx_ok = ULONG_MAX;
    stats.tx_errors = ULONG_MAX;
    stats.tx_busy = ULONG_MAX;
    stats.cad_timeouts = ULONG_MAX;
    stats.cad_timeout_sends = ULONG_MAX;
    stats.rx_rearm_failures = ULONG_MAX;

    daemon_stats_format_response(buf, sizeof(buf), LONG_MAX,
                                 RADIO_HEALTH_UNINITIALIZED, &stats);

    /* Worst case: every counter 20 digits — the last field must still be
     * complete and the line newline-terminated (no truncation). */
    expect_int("saturated stats keep last field intact",
               strstr(buf, "RXREARMFAIL=18446744073709551615\n") != NULL, 1);
}

static void test_stats_format_includes_cad_timeout(void)
{
    DaemonRadioStats stats;
    char buf[256];

    daemon_radio_stats_init(&stats);
    daemon_radio_stats_record_tx_result(&stats, TX_RESULT_CAD_TIMEOUT);

    daemon_stats_format_response(buf, sizeof(buf), 12, RADIO_HEALTH_READY, &stats);

    expect_int("stats has cad timeout", strstr(buf, "CADTIMEOUT=1 CADSEND=0") != NULL, 1);
}

static void test_uptime_crosses_32bit_ms_boundary(void)
{
    const DaemonTimeMs start = INT64_C(2147483600);

    daemon_stats_start(start);

    expect_long("uptime starts at zero",
                daemon_stats_uptime_seconds(start), 0);
    expect_long("uptime crosses 32-bit ms boundary",
                daemon_stats_uptime_seconds(start + INT64_C(1500)), 1);
}

static void test_stats_concurrent_record_and_format(void)
{
    DaemonRadioStats stats;
    char buf[256];
    const int iterations = 2000;

    daemon_radio_stats_init(&stats);

    std::thread tx([&stats, iterations]() {
        for (int i = 0; i < iterations; i++) {
            daemon_radio_stats_record_tx_result(&stats, TX_RESULT_OK);
            daemon_radio_stats_record_cad_timeout_send(&stats);
        }
    });

    std::thread rx([&stats, iterations]() {
        for (int i = 0; i < iterations; i++) {
            daemon_radio_stats_record_rx(&stats, 3);
            daemon_radio_stats_record_rx_drop(&stats);
        }
    });

    for (int i = 0; i < iterations; i++) {
        daemon_stats_format_response(buf, sizeof(buf), i, RADIO_HEALTH_READY, &stats);
        if (strstr(buf, "STATS UPTIME=") == NULL) {
            g_fail++;
            printf("[FAIL] concurrent stats format\n");
            break;
        }
    }

    tx.join();
    rx.join();

    expect_ulong("concurrent tx total", stats.tx_ok, (unsigned long)iterations);
    expect_ulong("concurrent cad-send total",
                 stats.cad_timeout_sends,
                 (unsigned long)iterations);
    expect_ulong("concurrent rx total", stats.rx_packets, (unsigned long)iterations);
    expect_ulong("concurrent rx bytes",
                 stats.rx_bytes,
                 (unsigned long)(iterations * 3));
    expect_ulong("concurrent rx-drop total",
                 stats.rx_drops,
                 (unsigned long)iterations);
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

    test_tx_result_counters();
    test_cad_timeout_counted_once_via_tx_result();
    test_stats_format_includes_cad_timeout();
    test_stats_format_saturated_counters_fit();
    test_uptime_crosses_32bit_ms_boundary();
    test_stats_concurrent_record_and_format();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
