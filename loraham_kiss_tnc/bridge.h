#ifndef LHKT_BRIDGE_H
#define LHKT_BRIDGE_H

#include "loraham_kiss_tnc.h"

#ifdef __cplusplus
extern "C" {
#endif

int lhkt_bridge_run(const lhkt_config_t *cfg, lhkt_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif
