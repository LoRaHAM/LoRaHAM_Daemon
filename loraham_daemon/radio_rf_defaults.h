#ifndef LORAHAM_RADIO_RF_DEFAULTS_H
#define LORAHAM_RADIO_RF_DEFAULTS_H

#include <stdint.h>

/* Boot RF defaults applied inside RadioDriver::begin() (chip-specific setter
 * order preserved). LDRO: <0 = autoLDRO() only, >=0 = autoLDRO() then
 * forceLDRO(value) -- mirrors the pre-driver per-band init exactly. */
typedef struct RadioRfDefaults {
    float freq_mhz;
    int spreading_factor;
    float bandwidth_khz;
    uint8_t sync_word;
    int preamble_len;
    int coding_rate;
    bool crc_on;
    int ldro;                /* <0: auto; 0/1: forced value */
    int power_dbm;
} RadioRfDefaults;

#endif
