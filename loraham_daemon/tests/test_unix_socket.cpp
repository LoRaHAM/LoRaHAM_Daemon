#include "../unix_socket.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * Unix socket lifecycle tests.
 *
 * These cover close/unlink cleanup before daemon shutdown code uses it.
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

static int path_exists(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0;
}

/* --- setup/close behavior --- */

static void test_close_unix_socket_unlinks_and_clears_fd(void)
{
    const char *path = "/tmp/loraham_test_unix_socket.sock";
    int fd;

    unlink(path);
    fd = setup_unix_socket(path, 1);

    expect_int("socket path exists after setup", path_exists(path), 1);
    expect_int("fd valid after setup", fd >= 0, 1);

    close_unix_socket(&fd, path);

    expect_int("fd cleared after close", fd, -1);
    expect_int("socket path removed after close", path_exists(path), 0);

    close_unix_socket(&fd, path);
    expect_int("close helper idempotent fd", fd, -1);
    expect_int("close helper idempotent path", path_exists(path), 0);
}

static void test_setup_rejects_regular_file_path(void)
{
    const char *path = "/tmp/loraham_test_unix_socket_file.sock";
    int file_fd;
    int socket_fd;

    unlink(path);
    file_fd = open(path, O_CREAT | O_WRONLY, 0600);
    expect_int("regular file created", file_fd >= 0, 1);
    if (file_fd >= 0)
        close(file_fd);

    socket_fd = setup_unix_socket(path, 1);

    expect_int("regular file path rejected", socket_fd, -1);
    expect_int("regular file preserved", path_exists(path), 1);

    unlink(path);
}

static void test_setup_replaces_stale_socket_path(void)
{
    const char *path = "/tmp/loraham_test_unix_socket_stale.sock";
    int fd1;
    int fd2;

    unlink(path);

    fd1 = setup_unix_socket(path, 1);
    expect_int("first socket setup", fd1 >= 0, 1);
    expect_int("first socket path exists", path_exists(path), 1);

    if (fd1 >= 0)
        close(fd1);

    fd2 = setup_unix_socket(path, 1);
    expect_int("stale socket path replaced", fd2 >= 0, 1);
    expect_int("socket path still exists after replace", path_exists(path), 1);

    close_unix_socket(&fd2, path);
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

    test_close_unix_socket_unlinks_and_clears_fd();
    test_setup_rejects_regular_file_path();
    test_setup_replaces_stale_socket_path();

    printf("\nSummary: ok=%d fail=%d\n", g_ok, g_fail);

    return g_fail ? 1 : 0;
}
