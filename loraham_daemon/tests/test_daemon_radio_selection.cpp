#include "../daemon_radio_selection.h"

#include <stdio.h>
#include <string.h>

/* --- Radio selection helper tests --- */

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

static void test_default_selection(void)
{
    daemon_radio_selection = DAEMON_RADIO_SELECTION_BOTH;

    expect_str("default name", daemon_radio_selection_name(daemon_radio_selection), "both");
    expect_int("default enables 433", daemon_radio_433_enabled(), 1);
    expect_int("default enables 868", daemon_radio_868_enabled(), 1);
}

static void test_parse_433(void)
{
    expect_int("parse 433", daemon_parse_radio_selection("433"), 1);
    expect_str("433 name", daemon_radio_selection_name(daemon_radio_selection), "433");
    expect_int("433 enables 433", daemon_radio_433_enabled(), 1);
    expect_int("433 disables 868", daemon_radio_868_enabled(), 0);
}

static void test_parse_868(void)
{
    expect_int("parse 868", daemon_parse_radio_selection("868"), 1);
    expect_str("868 name", daemon_radio_selection_name(daemon_radio_selection), "868");
    expect_int("868 disables 433", daemon_radio_433_enabled(), 0);
    expect_int("868 enables 868", daemon_radio_868_enabled(), 1);
}

static void test_parse_both(void)
{
    expect_int("parse both", daemon_parse_radio_selection("both"), 1);
    expect_str("both name", daemon_radio_selection_name(daemon_radio_selection), "both");
    expect_int("both enables 433", daemon_radio_433_enabled(), 1);
    expect_int("both enables 868", daemon_radio_868_enabled(), 1);
}

static void test_invalid_selection(void)
{
    daemon_radio_selection = DAEMON_RADIO_SELECTION_BOTH;

    expect_int("parse null fails", daemon_parse_radio_selection(NULL), 0);
    expect_int("parse invalid fails", daemon_parse_radio_selection("915"), 0);
    expect_str("invalid keeps previous selection",
               daemon_radio_selection_name(daemon_radio_selection), "both");
    expect_str("unknown enum name",
               daemon_radio_selection_name((DaemonRadioSelection)99), "unknown");
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

    test_default_selection();
    test_parse_433();
    test_parse_868();
    test_parse_both();
    test_invalid_selection();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
