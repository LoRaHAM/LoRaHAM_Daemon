#include "common_loradaemon_test.h"

/*
 * Known-issue tests.
 *
 * These describe desired future behavior. They are run as XFAIL so they
 * document bugfix targets without breaking the current baseline.
 */

static const char *g_bin = NULL;


/* --- Desired: parser should expose invalid numeric values as errors --- */

static int desired_strict_numeric_validation(void)
{
    /*
     * Current daemon has no stable OK/ERR response contract for invalid
     * numeric values like POWER=abc, CRC=abc, SF=12xyz, BW=125foo.
     */
    return TEST_FAIL;
}

/* --- CLI parsing and XFAIL sequence --- */

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--bin") == 0) {
            if (i + 1 >= argc) {
                usage_common(argv[0]);
                return 2;
            }
            g_bin = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 ||
                   strcmp(argv[i], "-h") == 0) {
            usage_common(argv[0]);
            return 0;
        } else {
            usage_common(argv[0]);
            return 2;
        }
    }

    if (!g_bin) {
        usage_common(argv[0]);
        return 2;
    }

    info_msg("starting daemon: %s", g_bin);
    if (start_daemon(g_bin) < 0)
        return 1;

    if (wait_all_sockets(DEFAULT_SOCKET_TIMEOUT_MS) < 0) {
        stop_daemon();
        return 1;
    }

    run_xfail_test("desired: strict numeric validation has observable errors",
                   desired_strict_numeric_validation);

    if (!daemon_alive()) {
        fail_msg("daemon exited during known-issue tests");
        g_fail++;
    }

    stop_daemon();
    return print_summary();
}
