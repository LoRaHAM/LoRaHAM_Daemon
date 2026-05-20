#ifndef LORAHAM_CONFIG_VALIDATE_H
#define LORAHAM_CONFIG_VALIDATE_H

#include "config_parser.h"
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

bool config_validate_command(const ConfigCommand &cmd,
                             RadioMode_t current_mode,
                             ConfigValidationResult *result);

#endif
