/*
 * Packaging/deployment regression test (M4-P0-1 / M4-P1).
 *
 * Verifies that the deployable systemd artifacts exist in the source tree and
 * document the correct shared-lock path, and that the unit file does not carry
 * the patterns that would split or relocate the shared lock namespace. This
 * guards against the artifact being silently dropped (e.g. by a .gitignore rule)
 * or the unit regressing to RuntimeDirectory / EnvironmentFile.
 *
 * The project directory is derived from /proc/self/exe so the test is
 * location-independent.
 */

#include <cstdio>
#include <cstring>
#include <cstdlib>

#include <limits.h>
#include <unistd.h>

static int g_ok = 0;
static int g_fail = 0;

static void expect(const char *name, int cond)
{
    if (cond) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s\n", name);
    }
}

/* Read up to cap-1 bytes of `path` into buf (NUL-terminated). Returns 1 on
 * success. */
static int read_file(const char *path, char *buf, size_t cap)
{
    FILE *f = fopen(path, "rb");
    size_t n;

    if (!f)
        return 0;

    n = fread(buf, 1, cap - 1, f);
    buf[n] = '\0';
    fclose(f);
    return 1;
}

int main(void)
{
    char exe[PATH_MAX];
    char proj[PATH_MAX];
    char path[PATH_MAX + 128];
    char buf[8192];
    ssize_t n;
    char *slash;

    n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0) {
        printf("[FAIL] cannot resolve /proc/self/exe\n");
        printf("\nSummary: ok=0 fail=1\n");
        return 1;
    }
    exe[n] = '\0';

    /* exe = <proj>/tests/test_packaging -> strip two path components. */
    snprintf(proj, sizeof(proj), "%s", exe);
    slash = strrchr(proj, '/');
    if (slash) *slash = '\0';          /* drop /test_packaging */
    slash = strrchr(proj, '/');
    if (slash) *slash = '\0';          /* drop /tests */

    /* tmpfiles artifact exists and documents /run/lock/loraham. */
    snprintf(path, sizeof(path), "%s/systemd/tmpfiles.d/loraham.conf", proj);
    expect("tmpfiles.d/loraham.conf exists", read_file(path, buf, sizeof(buf)));
    expect("tmpfiles rule provisions /run/lock/loraham",
           strstr(buf, "/run/lock/loraham") != NULL);
    expect("tmpfiles rule provisions /run/loraham socket dir",
           strstr(buf, "/run/loraham ") != NULL ||
           strstr(buf, "/run/loraham\t") != NULL ||
           strstr(buf, "d      /run/loraham") != NULL);
    expect("socket dir is setgid, owned by the daemon user (2750)",
           strstr(buf, "2750 loraham loraham") != NULL);
    expect("lock dir is owned by the daemon user",
           strstr(buf, "/run/lock/loraham   0755 loraham loraham") != NULL);
    expect("tmpfiles rule is a directory rule (starts with d)",
           strstr(buf, "\nd ") != NULL || buf[0] == 'd');

    /* systemd unit exists and is free of namespace-splitting patterns. */
    snprintf(path, sizeof(path), "%s/systemd/loraham-daemon@.service", proj);
    expect("loraham-daemon@.service exists", read_file(path, buf, sizeof(buf)));
    /* Non-root deployment: dedicated unprivileged user, hardware via the
     * spi/gpio device groups. Client separation is carried by 0600 lock
     * files, not by the process group. */
    expect("unit runs as user loraham", strstr(buf, "User=loraham\n") != NULL);
    expect("unit does NOT run as root", strstr(buf, "User=root") == NULL);
    expect("unit gets hardware via spi/gpio groups",
           strstr(buf, "SupplementaryGroups=spi gpio") != NULL);
    expect("unit sets UMask=0007", strstr(buf, "UMask=0007") != NULL);
    expect("unit clears socket-dir override",
           strstr(buf, "LORAHAM_SOCKET_DIR") != NULL);
    /* Audit item 4: only startup codes 3/4 are restart-prevented; the
     * runtime SPI fatal (5) must stay eligible for Restart=on-failure. */
    expect("restart prevention is exactly 3 4",
           strstr(buf, "RestartPreventExitStatus=3 4\n") != NULL);
    expect("runtime SPI fatal (5) is restartable",
           strstr(buf, "RestartPreventExitStatus=3 4 5") == NULL);
    /* Check for the directive form (Key=), so explanatory comments mentioning
     * the words do not trip the test. */
    expect("unit has no RuntimeDirectory= directive",
           strstr(buf, "RuntimeDirectory=") == NULL);
    expect("unit has no EnvironmentFile= directive",
           strstr(buf, "EnvironmentFile=") == NULL);
    /* Production must be pinned to /run/lock/loraham: clear any inherited
     * manager-level LORAHAM_RUNTIME_DIR so the two band services cannot end up
     * with split lock namespaces. */
    expect("unit pins lock namespace (UnsetEnvironment=LORAHAM_RUNTIME_DIR)",
           strstr(buf, "UnsetEnvironment=LORAHAM_RUNTIME_DIR") != NULL);
    /* No dangling Documentation= path that the install steps do not provide. */
    expect("unit has no Documentation= directive",
           strstr(buf, "Documentation=") == NULL);
    expect("unit documents stable exit codes via RestartPreventExitStatus",
           strstr(buf, "RestartPreventExitStatus") != NULL);
    expect("unit runs foreground (Type=simple)",
           strstr(buf, "Type=simple") != NULL);

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
