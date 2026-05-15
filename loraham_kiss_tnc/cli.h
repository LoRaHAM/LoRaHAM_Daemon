#ifndef LHKT_CLI_H
#define LHKT_CLI_H

#include "loraham_kiss_tnc.h"

#ifdef __cplusplus
extern "C" {
#endif

int lhkt_cli_apply(int argc, char **argv, lhkt_config_t *cfg);
void lhkt_cli_print_usage(const char *prog);
void lhkt_cli_print_config(const lhkt_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif
