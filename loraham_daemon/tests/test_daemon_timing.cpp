#include "../daemon_timing.h"
#include "../daemon_monitoring.h"
#include "../daemon_protocol.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/*
 * Daemon timing unit tests.
 *
 * These tests lock the current counter-based tick behavior before deadline
 * timers are introduced.
 */

static int g_ok = 0;
static int g_fail = 0;

/* --- Test helpers --- */

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

static void expect_time(const char *name,
                        DaemonTimeMs actual,
                        DaemonTimeMs expected)
{
    if (actual == expected) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: expected %" PRId64 ", got %" PRId64 "\n",
               name, (int64_t)expected, (int64_t)actual);
    }
}

/* --- Counter tick behavior --- */

static void test_plain_counter_tick(void)
{
    int counter = 0;

    expect_int("plain tick 1 not due", daemon_tick_due(&counter, 3), 0);
    expect_int("plain tick 1 counter", counter, 1);

    expect_int("plain tick 2 not due", daemon_tick_due(&counter, 3), 0);
    expect_int("plain tick 2 counter", counter, 2);

    expect_int("plain tick 3 due", daemon_tick_due(&counter, 3), 1);
    expect_int("plain tick reset", counter, 0);
}

static void test_state_counter_tick(void)
{
    DaemonTick tick;

    daemon_tick_init(&tick, 2);

    expect_int("state init counter", tick.counter, 0);
    expect_int("state init interval", tick.interval, 2);

    expect_int("state tick 1 not due", daemon_tick_state_due(&tick), 0);
    expect_int("state tick 1 counter", tick.counter, 1);

    expect_int("state tick 2 due", daemon_tick_state_due(&tick), 1);
    expect_int("state tick reset", tick.counter, 0);
}


static void test_deadline_timer(void)
{
    DaemonDeadlineTimer timer;

    daemon_deadline_timer_init(&timer, 1000, 100);

    expect_int("deadline init interval", (int)timer.interval_ms, 100);
    expect_int("deadline init next due", (int)timer.next_due_ms, 1100);

    expect_int("deadline timeout before due",
               (int)daemon_deadline_timer_timeout_ms(&timer, 1050), 50);
    expect_int("deadline not due before due",
               daemon_deadline_timer_due(&timer, 1099), 0);
    expect_int("deadline due at due time",
               daemon_deadline_timer_due(&timer, 1100), 1);
    expect_int("deadline advances once", (int)timer.next_due_ms, 1200);

    expect_int("deadline catches up after delay",
               daemon_deadline_timer_due(&timer, 1350), 1);
    expect_int("deadline catch-up next due", (int)timer.next_due_ms, 1400);
    expect_int("deadline timeout when overdue",
               (int)daemon_deadline_timer_timeout_ms(&timer, 1400), 0);
}


static void test_deadline_timer_crosses_32bit_ms(void)
{
    const DaemonTimeMs start = INT64_C(2147483600);
    DaemonDeadlineTimer timer;

    daemon_deadline_timer_init(&timer, start, INT64_C(100));

    expect_time("deadline crosses 32-bit next due",
                timer.next_due_ms, start + INT64_C(100));
    expect_time("deadline timeout across 32-bit boundary",
                daemon_deadline_timer_timeout_ms(&timer, start),
                INT64_C(100));
    expect_int("deadline waits across 32-bit boundary",
               daemon_deadline_timer_due(&timer, start + INT64_C(99)),
               0);
    expect_int("deadline fires across 32-bit boundary",
               daemon_deadline_timer_due(&timer, start + INT64_C(100)),
               1);
    expect_time("deadline advances across 32-bit boundary",
                timer.next_due_ms, start + INT64_C(200));
    expect_int("deadline catches up across 32-bit boundary",
               daemon_deadline_timer_due(&timer, start + INT64_C(450)),
               1);
    expect_time("deadline catches up with 64-bit arithmetic",
                timer.next_due_ms, start + INT64_C(500));
}

static void test_monitoring_cad_gate(void)
{
    DaemonDeadlineTimer timer;
    int probes = 0;
    DaemonTimeMs last_probe = 0;

    daemon_deadline_timer_init(&timer, 1000, DAEMON_CAD_POLL_INTERVAL_MS);

    for (DaemonTimeMs now = 1000; now <= 2000; now += 50) {
        if (daemon_monitoring_cad_probe_due(&timer, now, 1)) {
            if (last_probe > 0) {
                expect_int("CAD gate interval",
                           (int)(now - last_probe),
                           DAEMON_CAD_POLL_INTERVAL_MS);
            }
            last_probe = now;
            probes++;
        }
    }

    expect_int("CAD gate probe count", probes, 5);

    daemon_deadline_timer_init(&timer, 1000, DAEMON_CAD_POLL_INTERVAL_MS);
    probes = 0;
    for (DaemonTimeMs now = 1000; now <= 2000; now += 50) {
        if (daemon_monitoring_cad_probe_due(&timer, now, 0))
            probes++;
    }

    expect_int("CAD gate no CONF client", probes, 0);
    expect_int("CAD gate first subscriber probe",
               daemon_monitoring_cad_probe_due(&timer, 2000, 1),
               1);
}



static void test_cad_broadcast_edges(void)
{
    expect_int("CAD edge inactive to busy",
               daemon_monitoring_cad_broadcast_edge(0, 1),
               1);
    expect_int("CAD edge busy remains busy",
               daemon_monitoring_cad_broadcast_edge(1, 1),
               0);
    expect_int("CAD edge busy to inactive",
               daemon_monitoring_cad_broadcast_edge(1, 0),
               -1);
    expect_int("CAD edge inactive remains inactive",
               daemon_monitoring_cad_broadcast_edge(0, 0),
               0);
}

static void test_tx_status_generation_order(void)
{
    uint32_t observed_generation = 0;

    expect_int("TX status first transition busy",
               daemon_monitoring_tx_status_next_busy(
                   observed_generation),
               1);

    observed_generation++;
    expect_int("TX status second transition idle",
               daemon_monitoring_tx_status_next_busy(
                   observed_generation),
               0);

    observed_generation++;
    expect_int("TX status third transition busy",
               daemon_monitoring_tx_status_next_busy(
                   observed_generation),
               1);
}

static void test_monotonic_now_ms(void)
{
    DaemonTimeMs t1 = daemon_now_ms();
    usleep(1000);
    DaemonTimeMs t2 = daemon_now_ms();

    if (t2 >= t1) {
        g_ok++;
        printf("[ OK ] monotonic now does not go backwards\n");
    } else {
        g_fail++;
        printf("[FAIL] monotonic now went backwards: %" PRId64
               " -> %" PRId64 "\n",
               (int64_t)t1, (int64_t)t2);
    }
}

/* --- CLI parsing and test sequence --- */

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

    test_plain_counter_tick();
    test_state_counter_tick();
    test_deadline_timer();
    test_deadline_timer_crosses_32bit_ms();
    test_monitoring_cad_gate();
    test_cad_broadcast_edges();
    test_tx_status_generation_order();
    test_monotonic_now_ms();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
