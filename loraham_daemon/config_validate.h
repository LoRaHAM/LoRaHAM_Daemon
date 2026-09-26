#ifndef LORAHAM_CONFIG_VALIDATE_H
#define LORAHAM_CONFIG_VALIDATE_H

#include "config_parser.h"
#include "hardware_profile.h"   /* DaemonChipFamily */
#include "radio_channel.h"

#include <string>

/* --- Transactional CONFIG validation --- */

struct ConfigValidationResult {
    bool valid;
    std::string key;
    std::string value;
    std::string reason;
    RadioMode_t target_mode;
};

void config_validation_result_init(ConfigValidationResult *result,
                                   RadioMode_t current_mode);

/* chip_family selects family-specific value rasters (FSK RXBW, POWER).
 * freq_min/max_mhz is the band's operational frequency policy — deliberately without defaults so no caller can
 * silently validate FREQ against an unbounded range.
 * high_power is the process's --high-power permission (daemon_high_power_boot.h): it admits exactly
 * POWER=20 on an SX127x board and nothing else. Deliberately without a default, for the same
 * reason: the whole-command prevalidation is where a POWER=20 without permission is refused, before
 * any hardware effect, and every caller must say which permission it holds. */
bool config_validate_command(const ConfigCommand &cmd,
                             RadioMode_t current_mode,
                             ConfigValidationResult *result,
                             DaemonChipFamily chip_family,
                             float freq_min_mhz,
                             float freq_max_mhz,
                             bool high_power);

#endif
