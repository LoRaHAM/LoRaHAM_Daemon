#include "bridge.h"
#include "cli.h"
#include "loraham_kiss_tnc.h"

#include <signal.h>
#include <stdio.h>

/*
 * Program entry point.
 * Detailed CLI handling and bridge logic live in separate modules.
 */

int main(int argc, char **argv)
{
    lhkt_config_t cfg;
    lhkt_stats_t stats;
    int ret;

    signal(SIGPIPE, SIG_IGN);

    lhkt_config_defaults(&cfg);
    lhkt_stats_init(&stats);

    ret = lhkt_cli_apply(argc, argv, &cfg);
    if (ret != LHKT_OK) {
        return 1;
    }

    printf("[Init] LoRaHAM KISS TNC bridge\n");
    lhkt_cli_print_config(&cfg);

    ret = lhkt_bridge_run(&cfg, &stats);

    if (cfg.stats_interval > 0 || cfg.verbose) {
        lhkt_stats_print(&stats);
    }

    return ret;
}
