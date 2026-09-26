/*
 * Multi-instance (split per-band) integration test.
 *
 * Verifies that the daemon can run as two independent per-band processes:
 *   - a duplicate same-band instance is rejected and CANNOT steal the live
 *     instance's sockets (the LED/ownership claim happens before any socket
 *     unlink/bind);
 *   - a 433 and an 868 instance run simultaneously;
 *   - stopping one band leaves the other untouched.
 *
 * Requires real radio hardware: a per-band instance only stays up once its
 * radio reports ready.  The check is for the precondition -- no SPI device, so
 * no radio can be probed -- and the whole test reports SKIP.  Where an SPI
 * device exists the test runs for real and an instance that does not come up
 * is a FAILURE.
 */

#include "common_loradaemon_test.h"

static const char *g_bin = NULL;

/* Daemons spawned by this test; all are force-killed on every exit path so the
 * suite's lingering-daemon guard stays happy. */
static pid_t g_pids[8];
static int g_pid_count = 0;

static pid_t spawn_daemon(const char *radio)
{
    pid_t pid;

    ensure_test_runtime_dir();

    pid = fork();

    if (pid < 0)
        return -1;

    if (pid == 0) {
        setpgid(0, 0);
        execl(g_bin, g_bin, "--radio", radio, (char *)NULL);
        _exit(127);
    }

    (void)setpgid(pid, pid);

    if (g_pid_count < ARRAY_LEN(g_pids))
        g_pids[g_pid_count++] = pid;

    return pid;
}

static int pid_alive(pid_t pid)
{
    return pid > 0 && kill(pid, 0) == 0;
}

static void kill_pid(pid_t pid)
{
    int status;

    if (pid <= 0)
        return;

    if (kill(-pid, SIGTERM) != 0)
        kill(pid, SIGTERM);

    for (int i = 0; i < 30; i++) {
        if (waitpid(pid, &status, WNOHANG) == pid)
            return;
        usleep(100000);
    }

    if (kill(-pid, SIGKILL) != 0)
        kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
}

static void cleanup_all(void)
{
    for (int i = 0; i < g_pid_count; i++)
        kill_pid(g_pids[i]);
    g_pid_count = 0;
}

/* Wait for a daemon to either publish `sock` (ready) or exit early. Returns
 * 1 if the socket appeared while the process is alive, 0 otherwise. */
static int wait_band_up(pid_t pid, const char *sock)
{
    if (wait_for_socket(sock, DEFAULT_SOCKET_TIMEOUT_MS) != 0)
        return 0;

    return pid_alive(pid);
}

static ino_t socket_inode(const char *path)
{
    struct stat st;

    if (stat(path, &st) != 0)
        return 0;

    return st.st_ino;
}

/* Wait up to timeout_ms for `pid` to exit; report its exit code via *code. */
static int wait_exit(pid_t pid, int timeout_ms, int *code)
{
    long deadline = now_ms() + timeout_ms;

    while (now_ms() < deadline) {
        int status;
        pid_t r = waitpid(pid, &status, WNOHANG);

        if (r == pid) {
            /* This pid is now reaped; drop it from the kill list. */
            for (int i = 0; i < g_pid_count; i++) {
                if (g_pids[i] == pid)
                    g_pids[i] = -1;
            }
            if (code)
                *code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            return 1;
        }

        usleep(50000);
    }

    return 0;
}

static int test_multi_instance(void)
{
    pid_t a, b, c;
    ino_t inode_before, inode_after;
    int b_code = 0;

    cleanup_all();

    /* 1) First 433 instance. */
    /* Radio hardware is the precondition, not the daemon coming up: once an SPI
     * device exists, an instance that fails to start is a FAILURE, not a skip. */
    if (radio_hardware_missing())
        return TEST_SKIP;

    a = spawn_daemon("433");
    if (a < 0) {
        fail_msg("fork failed for 433 instance");
        return TEST_FAIL;
    }

    if (!wait_band_up(a, SOCK_CONF_433)) {
        fail_msg("433 instance did not come up");
        cleanup_all();
        return TEST_FAIL;
    }

    inode_before = socket_inode(SOCK_CONF_433);

    /* 2) Duplicate 433 instance must be rejected and must NOT steal sockets. */
    b = spawn_daemon("433");
    if (b < 0) {
        fail_msg("fork failed for duplicate 433 instance");
        cleanup_all();
        return TEST_FAIL;
    }

    if (!wait_exit(b, DEFAULT_SOCKET_TIMEOUT_MS, &b_code)) {
        fail_msg("duplicate 433 instance did not exit (it should be rejected)");
        cleanup_all();
        return TEST_FAIL;
    }

    if (b_code == 0) {
        fail_msg("duplicate 433 instance exited 0 (expected non-zero rejection)");
        cleanup_all();
        return TEST_FAIL;
    }

    if (!pid_alive(a)) {
        fail_msg("first 433 instance died when duplicate started");
        cleanup_all();
        return TEST_FAIL;
    }

    inode_after = socket_inode(SOCK_CONF_433);
    if (inode_before == 0 || inode_before != inode_after) {
        fail_msg("433 conf socket was replaced by the duplicate (takeover!)");
        cleanup_all();
        return TEST_FAIL;
    }

    if (connect_unix_retry(SOCK_CONF_433, 2000) < 0) {
        fail_msg("433 conf socket not connectable after duplicate rejection");
        cleanup_all();
        return TEST_FAIL;
    }

    /* 3) An 868 instance must coexist with the running 433 instance. */
    c = spawn_daemon("868");
    if (c < 0) {
        fail_msg("fork failed for 868 instance");
        cleanup_all();
        return TEST_FAIL;
    }

    if (!wait_band_up(c, SOCK_CONF_868)) {
        fail_msg("868 instance did not come up");
        cleanup_all();
        return TEST_FAIL;
    }

    if (!pid_alive(a) || !pid_alive(c)) {
        fail_msg("433 and 868 instances are not both alive");
        cleanup_all();
        return TEST_FAIL;
    }

    /* The 868 instance must not own the 433 sockets and vice versa. */
    if (path_exists(SOCK_CONF_433) == 0 || path_exists(SOCK_CONF_868) == 0) {
        fail_msg("expected both per-band conf sockets to exist");
        cleanup_all();
        return TEST_FAIL;
    }

    /* 4) Stopping 433 must leave 868 untouched. */
    kill_pid(a);
    usleep(500000);

    if (!pid_alive(c)) {
        fail_msg("868 instance died when 433 was stopped");
        cleanup_all();
        return TEST_FAIL;
    }

    if (connect_unix_retry(SOCK_CONF_868, 2000) < 0) {
        fail_msg("868 conf socket not connectable after 433 stopped");
        cleanup_all();
        return TEST_FAIL;
    }

    cleanup_all();
    return TEST_PASS;
}

int main(int argc, char *argv[])
{
    char runtime_dir[128];

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--bin") == 0 && i + 1 < argc)
            g_bin = argv[++i];
    }

    if (!g_bin) {
        usage_common(argv[0]);
        return 2;
    }

    /* Point the spawned daemons at a writable shared lock dir (the production
     * default /run/lock/loraham fails closed for a non-root test runner). Both
     * children inherit this via the environment. */
    snprintf(runtime_dir, sizeof(runtime_dir),
             "/tmp/loraham-mi-%d", (int)getpid());
    mkdir(runtime_dir, 0755);
    setenv("LORAHAM_RUNTIME_DIR", runtime_dir, 1);

    run_test("multi_instance split per-band", test_multi_instance);

    cleanup_all();
    return print_summary();
}
