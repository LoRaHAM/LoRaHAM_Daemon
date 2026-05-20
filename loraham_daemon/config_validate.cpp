#include "config_validate.h"

#include "config_policy.h"
#include "config_value.h"

#include <stdint.h>

/* --- Helpers ------------------------------------------------------------- */

void config_validation_result_init(ConfigValidationResult *result,
                                   RadioMode_t current_mode)
{
    if (!result)
        return;

    result->valid = true;
    result->key.clear();
    result->value.clear();
    result->reason.clear();
    result->target_mode = current_mode;
}

static void config_validation_reject(ConfigValidationResult *result,
                                     const std::string &key,
                                     const std::string &value,
                                     const char *reason)
{
    if (!result)
        return;

    result->valid = false;
    result->key = key;
    result->value = value;
    result->reason = reason ? reason : "invalid";
}

static bool config_is_lora_only_key(const std::string &key)
{
    return key == "SF" || key == "BW" || key == "CR" ||
           key == "LDRO" || key == "CRC";
}

static bool config_is_fsk_only_key(const std::string &key)
{
    return key == "BR" || key == "FREQDEV" || key == "RXBW" ||
           key == "OOK" || key == "SHAPING" || key == "ENCODING";
}

static bool config_is_common_key(const std::string &key)
{
    return key == "FREQ" || key == "POWER" ||
           key == "PREAMBLE" || key == "SYNC";
}

static bool config_is_known_config_key(const std::string &key)
{
    return key == "GETRSSI" ||
           config_is_common_key(key) ||
           config_is_lora_only_key(key) ||
           config_is_fsk_only_key(key);
}

static bool config_validate_freq_value(const std::string &val)
{
    float f = 0.0f;

    return config_value_parse_float_exact(val, &f) && f > 0.0f;
}

static bool config_validate_power_value(const std::string &val)
{
    int p = 0;

    return config_value_parse_int_exact(val, &p) &&
           config_policy_power_valid(p);
}

static bool config_validate_lora_value(const std::string &key,
                                       const std::string &val)
{
    if (key == "FREQ")
        return config_validate_freq_value(val);

    if (key == "POWER")
        return config_validate_power_value(val);

    if (key == "SF") {
        int sf = 0;
        return config_value_parse_int_exact(val, &sf) &&
               config_policy_lora_sf_valid(sf);
    }

    if (key == "BW") {
        float bw = 0.0f;
        return config_value_parse_float_exact(val, &bw) &&
               config_policy_lora_bandwidth_valid(bw);
    }

    if (key == "CR") {
        int cr = 0;
        return config_value_parse_int_exact(val, &cr) &&
               config_policy_lora_cr_valid(cr);
    }

    if (key == "CRC")
        return config_value_parse_bool01_exact(val, NULL);

    if (key == "PREAMBLE") {
        int preamble = 0;
        return config_value_parse_int_exact(val, &preamble) &&
               config_policy_lora_preamble_valid(preamble);
    }

    if (key == "SYNC") {
        uint32_t sync = 0;
        return config_value_parse_hex_or_dec_u32_exact(val, &sync) &&
               config_policy_lora_sync_valid(sync);
    }

    if (key == "LDRO") {
        std::string norm = config_value_lower_ascii(config_value_trim_ascii(val));
        if (norm == "auto")
            return true;

        return config_value_parse_bool01_exact(val, NULL);
    }

    return true;
}

static bool config_validate_fsk_shaping_value(const std::string &val)
{
    std::string norm = config_value_lower_ascii(config_value_trim_ascii(val));

    if (norm == "off" || norm == "none")
        return true;

    float shaping = 0.0f;
    if (!config_value_parse_float_exact(norm, &shaping))
        return false;

    return config_value_float_equal(shaping, 0.0f) ||
           config_value_float_equal(shaping, 0.3f) ||
           config_value_float_equal(shaping, 0.5f) ||
           config_value_float_equal(shaping, 0.7f) ||
           config_value_float_equal(shaping, 1.0f);
}

static bool config_validate_fsk_value(const std::string &key,
                                      const std::string &val)
{
    if (key == "FREQ")
        return config_validate_freq_value(val);

    if (key == "POWER")
        return config_validate_power_value(val);

    if (key == "BR") {
        float br = 0.0f;
        return config_value_parse_float_exact(val, &br) &&
               config_policy_fsk_bitrate_valid(br);
    }

    if (key == "FREQDEV") {
        float freqdev = 0.0f;
        return config_value_parse_float_exact(val, &freqdev) &&
               config_policy_fsk_freqdev_valid(freqdev);
    }

    if (key == "RXBW") {
        float bw = 0.0f;
        return config_value_parse_float_exact(val, &bw) &&
               config_policy_fsk_rxbw_valid(bw);
    }

    if (key == "OOK")
        return config_value_parse_bool01_exact(val, NULL);

    if (key == "SHAPING")
        return config_validate_fsk_shaping_value(val);

    if (key == "ENCODING") {
        int encoding = 0;
        return config_value_parse_int_exact(val, &encoding) &&
               encoding >= 0 && encoding <= 2;
    }

    if (key == "PREAMBLE") {
        int preamble = 0;
        return config_value_parse_int_exact(val, &preamble) &&
               config_policy_fsk_preamble_valid(preamble);
    }

    if (key == "SYNC") {
        uint32_t sync = 0;
        return config_value_parse_hex_or_dec_u32_exact(val, &sync) &&
               config_policy_fsk_sync_valid(sync);
    }

    return true;
}

/* --- Whole-command validation ------------------------------------------- */

bool config_validate_command(const ConfigCommand &cmd,
                             RadioMode_t current_mode,
                             ConfigValidationResult *result)
{
    config_validation_result_init(result, current_mode);

    if (!cmd.is_set || !cmd.has_params)
        return true;

    if (!cmd.malformed_tokens.empty()) {
        config_validation_reject(result, cmd.malformed_tokens[0],
                                 "",
                                 "malformed token");
        return false;
    }

    RadioMode_t target_mode = current_mode;

    if (!cmd.mode.empty()) {
        if (cmd.mode == "FSK") {
            target_mode = RADIO_MODE_FSK;
        } else if (cmd.mode == "LORA") {
            target_mode = RADIO_MODE_LORA;
        } else {
            config_validation_reject(result, "MODE", cmd.mode,
                                     "unknown mode");
            return false;
        }
    }

    if (result)
        result->target_mode = target_mode;

    for (size_t i = 0; i < cmd.tokens.size(); i++) {
        const std::string &key = cmd.tokens[i].first;
        const std::string &val = cmd.tokens[i].second;

        if (!config_is_known_config_key(key)) {
            config_validation_reject(result, key, val,
                                     "unknown key");
            return false;
        }

        if (key == "GETRSSI") {
            if (!config_value_parse_bool01_exact(val, NULL)) {
                config_validation_reject(result, key, val,
                                         "invalid boolean");
                return false;
            }

            continue;
        }

        if (target_mode == RADIO_MODE_FSK) {
            if (config_is_lora_only_key(key))
                continue;

            if (!config_validate_fsk_value(key, val)) {
                config_validation_reject(result, key, val,
                                         "invalid FSK value");
                return false;
            }
        } else {
            if (config_is_fsk_only_key(key))
                continue;

            if (!config_validate_lora_value(key, val)) {
                config_validation_reject(result, key, val,
                                         "invalid LoRa value");
                return false;
            }
        }
    }

    return true;
}
