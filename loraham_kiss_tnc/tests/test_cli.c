#include "cli.h"
#include "config.h"

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
    test_invalid_port();
    test_missing_config_file();
    test_help_exits_zero();

    puts("test_cli: OK");
    return 0;
}
