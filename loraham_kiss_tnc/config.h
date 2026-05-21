#ifndef LHKT_CONFIG_H
#define LHKT_CONFIG_H

#include "loraham_kiss_tnc.h"

#ifdef __cplusplus
extern "C" {
#endif

int lhkt_config_load_file(lhkt_config_t *cfg, const char *path);
int lhkt_config_parse_line(lhkt_config_t *cfg, char *line, unsigned int line_no);

#ifdef __cplusplus
}
#endif

#endif
