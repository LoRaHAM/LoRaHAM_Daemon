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

static bool config_validate_freq_value(const std::string &val,
                                       float freq_min_mhz,
                                       float freq_max_mhz)
{
    float f = 0.0f;

    return config_value_parse_float_exact(val, &f) &&
           config_policy_freq_valid_band(f, freq_min_mhz, freq_max_mhz);
}

static bool config_validate_power_value(const std::string &val,
                                        DaemonChipFamily chip_family,
                                        bool high_power)
{
    int p = 0;

    return config_value_parse_int_exact(val, &p) &&
           config_policy_power_valid_family(p, chip_family, high_power);
}

/* The one POWER refusal that has a remedy: exactly 20 on an SX127x board
 * without the boot permission. Named so the operator finds the switch
 * instead of reading "invalid value" for a number the datasheet lists. */
static bool config_validate_power_needs_permission(const std::string &val,
                                                   DaemonChipFamily chip_family,
                                                   bool high_power)
{
    int p = 0;

    return chip_family == DAEMON_CHIP_FAMILY_SX127X && !high_power &&
           config_value_parse_int_exact(val, &p) && p == 20;
}

static bool config_validate_lora_value(const std::string &key,
                                       const std::string &val,
                                       DaemonChipFamily chip_family,
                                       float freq_min_mhz,
                                       float freq_max_mhz,
                                       bool high_power)
{
    if (key == "FREQ")
        return config_validate_freq_value(val, freq_min_mhz, freq_max_mhz);

    if (key == "POWER")
        return config_validate_power_value(val, chip_family, high_power);

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
                                      const std::string &val,
                                      DaemonChipFamily chip_family,
                                      float freq_min_mhz,
                                      float freq_max_mhz,
                                      bool high_power)
{
    if (key == "FREQ")
        return config_validate_freq_value(val, freq_min_mhz, freq_max_mhz);

    if (key == "POWER")
        return config_validate_power_value(val, chip_family, high_power);

    if (key == "BR") {
        float br = 0.0f;
        return config_value_parse_float_exact(val, &br) &&
               config_policy_fsk_bitrate_valid(br);
    }

    if (key == "FREQDEV") {
        float freqdev = 0.0f;
        return config_value_parse_float_exact(val, &freqdev) &&
               config_policy_fsk_freqdev_valid_family(freqdev, chip_family);
    }

    if (key == "RXBW") {
        float bw = 0.0f;
        return config_value_parse_float_exact(val, &bw) &&
               config_policy_fsk_rxbw_valid_family(bw, chip_family);
    }

    if (key == "OOK") {
        int ook = 0;
        if (!config_value_parse_bool01_exact(val, &ook))
            return false;
        /* SX126x has no OOK: reject at prevalidation instead
         * of switching mode first and failing the key afterwards. */
        return config_policy_fsk_ook_valid_family(ook, chip_family);
    }

    if (key == "SHAPING")
        return config_validate_fsk_shaping_value(val);

    if (key == "ENCODING") {
        int encoding = 0;
        return config_value_parse_int_exact(val, &encoding) &&
               config_policy_fsk_encoding_valid_family(encoding, chip_family);
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

/* Distinct reason for band-policy hits: a client tuning an 868 process to
 * 433.9 sent perfectly valid syntax — "invalid value" would hide that it hit
 * the band policy, not a typo. */
static const char *config_validate_reject_reason(const std::string &key,
                                                 const std::string &val,
                                                 const char *generic,
                                                 float freq_min_mhz,
                                                 float freq_max_mhz,
                                                 DaemonChipFamily chip_family,
                                                 bool high_power)
{
    if (key == "POWER" &&
        config_validate_power_needs_permission(val, chip_family, high_power))
        return "high-power mode not enabled (start with --high-power)";

    if (key != "FREQ")
        return generic;

    float f = 0.0f;
    if (config_value_parse_float_exact(val, &f) && f > 0.0f &&
        !config_policy_freq_valid_band(f, freq_min_mhz, freq_max_mhz))
        return "off-band frequency (band policy)";

    return generic;
}

bool config_validate_command(const ConfigCommand &cmd,
                             RadioMode_t current_mode,
                             ConfigValidationResult *result,
                             DaemonChipFamily chip_family,
                             float freq_min_mhz,
                             float freq_max_mhz,
                             bool high_power)
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

    if (cmd.mode_count > 1) {
        config_validation_reject(result, "MODE", cmd.mode,
                                 "duplicate key");
        return false;
    }

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

        /* Duplicate keys rejected: sequential apply would
         * silently let the last one win — ambiguous client intent fails
         * closed instead. */
        for (size_t j = 0; j < i; j++) {
            if (cmd.tokens[j].first == key) {
                config_validation_reject(result, key, val,
                                         "duplicate key");
                return false;
            }
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

            if (!config_validate_fsk_value(key, val, chip_family,
                                           freq_min_mhz, freq_max_mhz,
                                           high_power)) {
                config_validation_reject(result, key, val,
                                         config_validate_reject_reason(
                                             key, val, "invalid FSK value",
                                             freq_min_mhz, freq_max_mhz,
                                             chip_family, high_power));
                return false;
            }
        } else {
            if (config_is_fsk_only_key(key))
                continue;

            if (!config_validate_lora_value(key, val, chip_family,
                                            freq_min_mhz, freq_max_mhz,
                                            high_power)) {
                config_validation_reject(result, key, val,
                                         config_validate_reject_reason(
                                             key, val, "invalid LoRa value",
                                             freq_min_mhz, freq_max_mhz,
                                             chip_family, high_power));
                return false;
            }
        }
    }

    return true;
}
