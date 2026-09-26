#ifndef LORAHAM_CONFIG_POLICY_H
#define LORAHAM_CONFIG_POLICY_H

#include <stddef.h>
#include <stdint.h>

#include "hardware_profile.h"   /* DaemonChipFamily */

/* --- CONFIG value policy --- */

bool config_policy_lora_sf_valid(int sf);
bool config_policy_lora_bandwidth_valid(float bw);
bool config_policy_lora_cr_valid(int cr);
bool config_policy_lora_preamble_valid(int preamble);
bool config_policy_lora_sync_valid(uint32_t sync);
/* --- LoRa low-data-rate optimisation ------------------------------------- */
/*
 * LDRO is mandated once the symbol time reaches 16 ms, because the crystal
 * drift over a long symbol stops being negligible. The boundary is
 * INCLUSIVE -- symbol time >= 16 ms -- matching RadioLib's own autoLDRO test.
 *
 * It lived in three places with two different boundaries: the driver's boot
 * path and RadioLib both used >=, while the airtime gate below used a strict >.
 * No SF/BW combination the validator accepts lands on exactly 16.000 ms, so
 * nothing ever differed on air -- but extracting a helper while leaving the
 * boundary ambiguous would only have preserved the disagreement in a new place.
 * This is the one definition; every caller uses it.
 */
bool config_policy_lora_ldro_required(int sf, float bw_khz);

bool config_policy_power_valid(int power);

/* Family-aware output power. SX127x: 2..17, plus exactly 20 when `high_power`
 * -- the process was started with --high-power -- is set. Below 2 RadioLib
 * switches to the RFO pin, which is not the antenna path on these boards.
 * 18 and 19 are refused with or without permission: the pinned RadioLib
 * PA_BOOST API admits 2..17 and exactly 20 (an API boundary, not a silicon
 * one). 20 is the datasheet's restricted +20 dBm mode and needs the explicit
 * boot permission; see config_policy.cpp for the contract. SX1262: 0..20,
 * permission irrelevant. */
bool config_policy_power_valid_family(int power, DaemonChipFamily family,
                                      bool high_power);

bool config_policy_fsk_bitrate_valid(float br);
bool config_policy_fsk_freqdev_valid(float freqdev);
bool config_policy_fsk_rxbw_valid(float bw);
bool config_policy_fsk_rxbw_valid_sx126x(float bw);
bool config_policy_fsk_rxbw_valid_family(float bw, DaemonChipFamily family);
bool config_policy_fsk_preamble_valid(int preamble);
bool config_policy_fsk_sync_valid(uint32_t sync);

bool config_policy_freq_valid_band(float freq_mhz,
                                   float min_mhz,
                                   float max_mhz);

bool config_policy_fsk_freqdev_valid_family(float freqdev,
                                            DaemonChipFamily family);
bool config_policy_fsk_ook_valid_family(int ook, DaemonChipFamily family);
bool config_policy_fsk_encoding_valid_family(int encoding,
                                             DaemonChipFamily family);

/* Airtime policy: worst-case (255-byte payload, CRC on)
 * per-packet airtime of an accepted configuration must stay below this,
 * comfortably under the systemd stop timeout (30 s). */
#define CONFIG_POLICY_MAX_AIRTIME_MS 20000.0

double config_policy_lora_airtime_ms(int sf, float bw_khz, int cr,
                                     int preamble, size_t payload_len);
double config_policy_fsk_airtime_ms(float br_kbps, int preamble_bits,
                                    size_t payload_len);

#endif
