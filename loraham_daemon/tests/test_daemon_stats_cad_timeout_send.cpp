#include "../daemon_stats.h"

#include <stdio.h>
#include <string.h>

/* --- CAD timeout send stats tests --------------------------------------- */

static int g_ok = 0;
static int g_fail = 0;

static void expect_ulong(const char *name,
                         unsigned long actual,
                         unsigned long expected)
{
    if (actual == expected) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: expected %lu, got %lu\n",
               name,
               expected,
               actual);
    }
}

static void expect_contains(const char *name,
                            const char *text,
                            const char *needle)
{
    if (text && needle && strstr(text, needle)) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: missing '%s' in '%s'\n",
               name,
               needle ? needle : "",
               text ? text : "");
    }
}

static void test_cad_timeout_send_counter(void)
{
    DaemonRadioStats stats;
    char fields[256];

    daemon_radio_stats_init(&stats);
    daemon_radio_stats_record_cad_timeout(&stats);
    daemon_radio_stats_record_cad_timeout_send(&stats);

    expect_ulong("cad timeout count", stats.cad_timeouts, 1);
    expect_ulong("cad timeout send count", stats.cad_timeout_sends, 1);

    daemon_stats_format_fields(fields,
                               sizeof(fields),
                               123,
                               RADIO_HEALTH_READY,
                               &stats);
    expect_contains("cad timeout formatted", fields, "CADTIMEOUT=1");
    expect_contains("cad timeout send formatted", fields, "CADSEND=1");
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

    test_cad_timeout_send_counter();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
