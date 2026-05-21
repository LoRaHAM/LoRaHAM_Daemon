#include "cli.h"
#include "config.h"
#include "version.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int argc_of(char **argv)
{
    int argc = 0;

    while (argv[argc]) {
        argc++;
    }

    return argc;
}

static void test_rx_only_and_port(void)
{
    lhkt_config_t cfg;
    char *argv[] = {
        "test_cli",
        "--rx-only",
        "--kiss-port", "9001",
        "--verbose",
        NULL
    };

    lhkt_config_defaults(&cfg);

    assert(lhkt_cli_apply(argc_of(argv), argv, &cfg) == LHKT_OK);
    assert(cfg.rx_only == 1);
    assert(cfg.kiss_port == 9001);
    assert(cfg.verbose == 1);
}

static void test_config_and_cli_override(void)
{
    const char *path = "/tmp/lhkt_cli_test.conf";
    FILE *fp;
    lhkt_config_t cfg;
    char *argv[] = {
        "test_cli",
        "--config", "/tmp/lhkt_cli_test.conf",
        "--kiss-port", "9005",
        "--data-socket", "/tmp/cli_lora.sock",
        "--rx-only",
        NULL
    };

    fp = fopen(path, "w");
    assert(fp != NULL);
    fprintf(fp, "kiss_port = 8002\n");
    fprintf(fp, "data_socket = /tmp/config_lora.sock\n");
    fprintf(fp, "rx_only = false\n");
    fclose(fp);

    lhkt_config_defaults(&cfg);

    assert(lhkt_cli_apply(argc_of(argv), argv, &cfg) == LHKT_OK);
    assert(cfg.kiss_port == 9005);
    assert(strcmp(cfg.data_socket, "/tmp/cli_lora.sock") == 0);
    assert(cfg.rx_only == 1);

    unlink(path);
}


static void test_timing_options(void)
{
    lhkt_config_t cfg;
    char *argv[] = {
        "test_cli",
        "--tx-settle-ms", "250",
        "--tx-return-ms", "750",
        NULL
    };

    lhkt_config_defaults(&cfg);

    assert(lhkt_cli_apply(argc_of(argv), argv, &cfg) == LHKT_OK);
    assert(cfg.tx_settle_ms == 250);
    assert(cfg.tx_return_ms == 750);
}

static void test_invalid_timing_option(void)
{
    lhkt_config_t cfg;
    char *argv[] = {
        "test_cli",
        "--tx-return-ms", "60001",
        NULL
    };

    lhkt_config_defaults(&cfg);

    assert(lhkt_cli_apply(argc_of(argv), argv, &cfg) == LHKT_ERR_FORMAT);
}

static void test_invalid_double_option(void)
{
    lhkt_config_t cfg;
    char *argv[] = {
        "test_cli",
        "--rx-freq", "nan",
        NULL
    };

    lhkt_config_defaults(&cfg);

    assert(lhkt_cli_apply(argc_of(argv), argv, &cfg) == LHKT_ERR_FORMAT);
}

static void test_invalid_port(void)
{
    lhkt_config_t cfg;
    char *argv[] = {
        "test_cli",
        "--kiss-port", "0",
        NULL
    };

    lhkt_config_defaults(&cfg);

    assert(lhkt_cli_apply(argc_of(argv), argv, &cfg) == LHKT_ERR_FORMAT);
}

static void test_missing_config_file(void)
{
    lhkt_config_t cfg;
    char *argv[] = {
        "test_cli",
        "--config", "/tmp/lhkt_missing_config_file.conf",
        NULL
    };

    unlink("/tmp/lhkt_missing_config_file.conf");
    lhkt_config_defaults(&cfg);

    assert(lhkt_cli_apply(argc_of(argv), argv, &cfg) != LHKT_OK);
}


static int run_cli_child_capture(char **argv, char *out, size_t out_size)
{
    int pipefd[2];
    pid_t pid;
    int status;
    size_t used = 0;

    assert(out != NULL);
    assert(out_size > 0);

    out[0] = '\0';

    /*
     * Flush inherited stdio buffers before fork(), then read the whole pipe.
     * Otherwise buffered parent output may be flushed by the child before the
     * version string and a single read() may miss the expected text.
     */
    fflush(NULL);

    assert(pipe(pipefd) == 0);

    pid = fork();
    assert(pid >= 0);

    if (pid == 0) {
        lhkt_config_t cfg;

        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);

        lhkt_config_defaults(&cfg);
        lhkt_cli_apply(argc_of(argv), argv, &cfg);

        fflush(NULL);
        _exit(1);
    }

    close(pipefd[1]);

    for (;;) {
        char buf[256];
        ssize_t n = read(pipefd[0], buf, sizeof(buf));

        if (n > 0) {
            size_t copy = (size_t)n;

            if (used + copy >= out_size)
                copy = out_size - 1 - used;

            if (copy > 0) {
                memcpy(out + used, buf, copy);
                used += copy;
                out[used] = '\0';
            }

            continue;
        }

        if (n == 0)
            break;

        break;
    }

    close(pipefd[0]);

    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status));

    return WEXITSTATUS(status);
}



static void test_version_exits_zero_and_prints_version(void)
{
    char out[4096];
    char *argv[] = {
        "test_cli",
        "--version",
        NULL
    };

    assert(run_cli_child_capture(argv, out, sizeof(out)) == 0);
    assert(strstr(out, LHKT_VERSION_TEXT) != NULL);
    assert(strstr(out, LHKT_VERSION) != NULL);
}

static void test_help_exits_zero(void)
{
    pid_t pid;
    int status;
    char *argv[] = {
        "test_cli",
        "--help",
        NULL
    };

    pid = fork();
    assert(pid >= 0);

    if (pid == 0) {
        lhkt_config_t cfg;

        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);

        lhkt_config_defaults(&cfg);
        lhkt_cli_apply(argc_of(argv), argv, &cfg);

        _exit(1);
    }

    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);
}

int main(void)
{
    test_rx_only_and_port();
    test_config_and_cli_override();
    test_timing_options();
    test_invalid_timing_option();
    test_invalid_double_option();
    test_invalid_port();
    test_missing_config_file();
    test_help_exits_zero();
    test_version_exits_zero_and_prints_version();

    puts("test_cli: OK");
    return 0;
}
