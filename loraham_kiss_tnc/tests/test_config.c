#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void test_defaults(void)
{
    lhkt_config_t cfg;

    lhkt_config_defaults(&cfg);

    assert(strcmp(cfg.kiss_host, "127.0.0.1") == 0);
    assert(cfg.kiss_port == 8001);
    assert(strcmp(cfg.data_socket, "/tmp/lora433.sock") == 0);
    assert(strcmp(cfg.conf_socket, "/tmp/loraconf433.sock") == 0);
    assert(cfg.rx_only == 0);
    assert(cfg.verbose == 0);
    assert(cfg.stats_interval == 60);
    assert(cfg.tx_settle_ms == 100);
    assert(cfg.tx_return_ms == 1000);
    assert(cfg.have_rx_freq == 1);
    assert(cfg.have_tx_freq == 1);
    assert(cfg.rx_freq > 433.774 && cfg.rx_freq < 433.776);
    assert(cfg.tx_freq > 433.899 && cfg.tx_freq < 433.901);
    assert(cfg.sf == 12);
    assert(cfg.bw == 125.0);
    assert(cfg.cr == 5);
    assert(cfg.crc == 1);
    assert(cfg.preamble == 8);
    assert(cfg.syncword == 0x12);
    assert(cfg.ldro == 1);
    assert(cfg.ldro_auto == 0);
    assert(cfg.power == 17);
}

static void test_parse_line(void)
{
    lhkt_config_t cfg;
    char line1[] = "kiss_port = 9001 # inline comment";
    char line2[] = "verbose = yes";
    char line3[] = "syncword = 0x34";
    char line4[] = "rx_freq = 433.775";
    char line5[] = "# full line comment";
    char line7[] = "tx_settle_ms = 250";
    char line8[] = "tx_return_ms = 750";
    char line9[] = "ldro = AUTO";
    char line10[] = "ldro = auto";
    char line11[] = "ldro = false";

    lhkt_config_defaults(&cfg);

    assert(lhkt_config_parse_line(&cfg, line1, 1) == LHKT_OK);
    assert(cfg.kiss_port == 9001);

    assert(lhkt_config_parse_line(&cfg, line2, 2) == LHKT_OK);
    assert(cfg.verbose == 1);

    assert(lhkt_config_parse_line(&cfg, line3, 3) == LHKT_OK);
    assert(cfg.syncword == 0x34);

    assert(lhkt_config_parse_line(&cfg, line4, 4) == LHKT_OK);
    assert(cfg.have_rx_freq == 1);
    assert(cfg.rx_freq > 433.774 && cfg.rx_freq < 433.776);

    assert(lhkt_config_parse_line(&cfg, line5, 5) == LHKT_OK);

    assert(lhkt_config_parse_line(&cfg, line7, 7) == LHKT_OK);
    assert(cfg.tx_settle_ms == 250);

    assert(lhkt_config_parse_line(&cfg, line8, 8) == LHKT_OK);
    assert(cfg.tx_return_ms == 750);

    cfg.ldro = 0;
    cfg.ldro_auto = 0;
    assert(lhkt_config_parse_line(&cfg, line9, 9) == LHKT_OK);
    assert(cfg.ldro == 0);
    assert(cfg.ldro_auto == 1);

    cfg.ldro = 0;
    cfg.ldro_auto = 0;
    assert(lhkt_config_parse_line(&cfg, line10, 10) == LHKT_OK);
    assert(cfg.ldro == 0);
    assert(cfg.ldro_auto == 1);

    assert(lhkt_config_parse_line(&cfg, line11, 11) == LHKT_OK);
    assert(cfg.ldro == 0);
    assert(cfg.ldro_auto == 0);
}

static void test_hash_only_comments(void)
{
    lhkt_config_t cfg;
    char line1[] = "; not a supported comment";
    char line2[] = "kiss_port = 8002 ; not a supported inline comment";

    lhkt_config_defaults(&cfg);

    assert(lhkt_config_parse_line(&cfg, line1, 1) == LHKT_ERR_FORMAT);
    assert(lhkt_config_parse_line(&cfg, line2, 2) == LHKT_ERR_FORMAT);
}

static void test_load_file(void)
{
    const char *path = "/tmp/lhkt_test.conf";
    FILE *fp;
    lhkt_config_t cfg;

    fp = fopen(path, "w");
    assert(fp != NULL);

    fprintf(fp, "# LoRaHAM KISS TNC test config\n");
    fprintf(fp, "kiss_host = 0.0.0.0\n");
    fprintf(fp, "kiss_port = 8100 # inline comment\n");
    fprintf(fp, "data_socket = /tmp/test_lora433.sock\n");
    fprintf(fp, "conf_socket = /tmp/test_loraconf433.sock\n");
    fprintf(fp, "rx_only = true\n");
    fprintf(fp, "verbose = on\n");
    fprintf(fp, "stats_interval = 30\n");
    fprintf(fp, "tx_settle_ms = 250\n");
    fprintf(fp, "tx_return_ms = 750\n");
    fprintf(fp, "rx_freq = 433.775\n");
    fprintf(fp, "tx_freq = 433.900\n");
    fprintf(fp, "sf = 11\n");
    fprintf(fp, "bw = 250.0\n");
    fprintf(fp, "cr = 6\n");
    fprintf(fp, "crc = 0\n");
    fprintf(fp, "preamble = 12\n");
    fprintf(fp, "syncword = 0x45\n");
    fprintf(fp, "ldro = 0\n");
    fprintf(fp, "power = 14\n");
    fclose(fp);

    lhkt_config_defaults(&cfg);

    assert(lhkt_config_load_file(&cfg, path) == LHKT_OK);

    assert(strcmp(cfg.kiss_host, "0.0.0.0") == 0);
    assert(cfg.kiss_port == 8100);
    assert(strcmp(cfg.data_socket, "/tmp/test_lora433.sock") == 0);
    assert(strcmp(cfg.conf_socket, "/tmp/test_loraconf433.sock") == 0);
    assert(cfg.rx_only == 1);
    assert(cfg.verbose == 1);
    assert(cfg.stats_interval == 30);
    assert(cfg.tx_settle_ms == 250);
    assert(cfg.tx_return_ms == 750);
    assert(cfg.have_rx_freq == 1);
    assert(cfg.have_tx_freq == 1);
    assert(cfg.rx_freq > 433.774 && cfg.rx_freq < 433.776);
    assert(cfg.tx_freq > 433.899 && cfg.tx_freq < 433.901);
    assert(cfg.sf == 11);
    assert(cfg.bw == 250.0);
    assert(cfg.cr == 6);
    assert(cfg.crc == 0);
    assert(cfg.preamble == 12);
    assert(cfg.syncword == 0x45);
    assert(cfg.ldro == 0);
    assert(cfg.ldro_auto == 0);
    assert(cfg.power == 14);

    unlink(path);
}

static void test_reject_invalid(void)
{
    lhkt_config_t cfg;
    char bad1[] = "kiss_port = 0";
    char bad2[] = "sf = 99";
    char bad3[] = "unknown_key = value";
    char bad4[] = "not_a_key_value_line";
    char bad5[] = "mode = LORA";
    char bad6[] = "mode = FSK";
    char bad7[] = "tx_return_ms = 60001";
    char bad8[] = "ldro = maybe";
    char bad9[] = "rx_freq = nan";
    char bad10[] = "tx_freq = inf";
    char bad11[] = "bw = -inf";
    char bad12[] = "sf = 6";
    char bad13[] = "power = -1";
    char bad14[] = "power = 21";

    lhkt_config_defaults(&cfg);

    assert(lhkt_config_parse_line(&cfg, bad1, 1) == LHKT_ERR_FORMAT);
    assert(lhkt_config_parse_line(&cfg, bad2, 2) == LHKT_ERR_FORMAT);
    assert(lhkt_config_parse_line(&cfg, bad3, 3) == LHKT_ERR_FORMAT);
    assert(lhkt_config_parse_line(&cfg, bad4, 4) == LHKT_ERR_FORMAT);
    assert(lhkt_config_parse_line(&cfg, bad5, 5) == LHKT_ERR_FORMAT);
    assert(lhkt_config_parse_line(&cfg, bad6, 6) == LHKT_ERR_FORMAT);
    assert(lhkt_config_parse_line(&cfg, bad7, 7) == LHKT_ERR_FORMAT);
    assert(lhkt_config_parse_line(&cfg, bad8, 8) == LHKT_ERR_FORMAT);
    assert(lhkt_config_parse_line(&cfg, bad9, 9) == LHKT_ERR_FORMAT);
    assert(lhkt_config_parse_line(&cfg, bad10, 10) == LHKT_ERR_FORMAT);
    assert(lhkt_config_parse_line(&cfg, bad11, 11) == LHKT_ERR_FORMAT);
    assert(lhkt_config_parse_line(&cfg, bad12, 12) == LHKT_ERR_FORMAT);
    assert(lhkt_config_parse_line(&cfg, bad13, 13) == LHKT_ERR_FORMAT);
    assert(lhkt_config_parse_line(&cfg, bad14, 14) == LHKT_ERR_FORMAT);
}

int main(void)
{
    test_defaults();
    test_parse_line();
    test_hash_only_comments();
    test_load_file();
    test_reject_invalid();

    puts("test_config: OK");
    return 0;
}
