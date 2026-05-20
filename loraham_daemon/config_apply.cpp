#include "config_apply.h"

#include "config_value.h"
#include "config_policy.h"

/* --- CONFIG value policy ------------------------------------------------- */

static void config_print_rejected(const char *key, const std::string &val)
{
    printf(" %s=\033[91;5m%s\033[0m", key, val.c_str());
}

static void config_print_state_int(const char *key, int value, int state)
{
    if (state == RADIOLIB_ERR_NONE)
        printf(" %s=\033[92m%d\033[0m", key, value);
    else
        printf(" %s=\033[91;5m%d\033[0m", key, value);
}

static void config_print_state_float(const char *key, float value, int state)
{
    if (state == RADIOLIB_ERR_NONE)
        printf(" %s=\033[92m%.3f\033[0m", key, value);
    else
        printf(" %s=\033[91;5m%.3f\033[0m", key, value);
}

/* --- FSK shaping --------------------------------------------------------- */

static bool parse_fsk_shaping(const std::string &val,
                              uint8_t *shaping,
                              const char **label)
{
    if (!shaping || !label)
        return false;

    std::string norm = config_value_lower_ascii(config_value_trim_ascii(val));

    if (norm == "off" || norm == "none") {
        *shaping = RADIOLIB_SHAPING_NONE;
        *label = "0.0";
        return true;
    }

    float value = 0.0f;
    if (!config_value_parse_float_exact(norm, &value))
        return false;

    if (config_value_float_equal(value, 0.0f)) {
        *shaping = RADIOLIB_SHAPING_NONE;
        *label = "0.0";
        return true;
    }

    if (config_value_float_equal(value, 0.3f)) {
        *shaping = RADIOLIB_SHAPING_0_3;
        *label = "0.3";
        return true;
    }

    if (config_value_float_equal(value, 0.5f)) {
        *shaping = RADIOLIB_SHAPING_0_5;
        *label = "0.5";
        return true;
    }

    if (config_value_float_equal(value, 0.7f)) {
        *shaping = RADIOLIB_SHAPING_0_7;
        *label = "0.7";
        return true;
    }

    if (config_value_float_equal(value, 1.0f)) {
        *shaping = RADIOLIB_SHAPING_1_0;
        *label = "1.0";
        return true;
    }

    return false;
}

/* --- LoRa CONFIG apply --------------------------------------------------- */

template<typename RadioT>
static void apply_lora_param_common(RadioT &radio,
                                    const char *tag,
                                    const std::string &key,
                                    const std::string &val)
{
    (void)tag;
    int state = 0;

    if (key == "SF") {
        int sf = 0;
        if (config_value_parse_int_exact(val, &sf) && config_policy_lora_sf_valid(sf)) {
            state = radio.setSpreadingFactor(sf);
            config_print_state_int("SF", sf, state);
        } else {
            config_print_rejected("SF", val);
        }
    }

    if (key == "BW") {
        float bw = 0.0f;
        if (config_value_parse_float_exact(val, &bw) && config_policy_lora_bandwidth_valid(bw)) {
            state = radio.setBandwidth(bw);
            config_print_state_float("BW", bw, state);
        } else {
            config_print_rejected("BW", val);
        }
    }

    if (key == "FREQ") {
        float f = 0.0f;
        if (config_value_parse_float_exact(val, &f) && f > 0.0f) {
            state = radio.setFrequency(f);
            if (state == RADIOLIB_ERR_NONE)
                printf(" FREQ=\033[92m%.6f\033[0m", f);
            else
                printf(" FREQ=\033[91;5m%.6f\033[0m", f);
        } else {
            config_print_rejected("FREQ", val);
        }
    }

    if (key == "CR") {
        int cr = 0;
        if (config_value_parse_int_exact(val, &cr) && config_policy_lora_cr_valid(cr)) {
            state = radio.setCodingRate(cr);
            config_print_state_int("CR", cr, state);
        } else {
            config_print_rejected("CR", val);
        }
    }

    if (key == "CRC") {
        int crc = 0;
        if (config_value_parse_bool01_exact(val, &crc)) {
            state = radio.setCRC(crc != 0);
            config_print_state_int("CRC", crc, state);
        } else {
            config_print_rejected("CRC", val);
        }
    }

    if (key == "PREAMBLE") {
        int pre = 0;
        if (config_value_parse_int_exact(val, &pre) && config_policy_lora_preamble_valid(pre)) {
            state = radio.setPreambleLength(pre);
            config_print_state_int("PREAMBLE", pre, state);
        } else {
            config_print_rejected("PREAMBLE", val);
        }
    }

    if (key == "SYNC") {
        uint32_t sw = 0;
        if (config_value_parse_hex_or_dec_u32_exact(val, &sw) && config_policy_lora_sync_valid(sw)) {
            state = radio.setSyncWord((uint8_t)sw);
            if (state == RADIOLIB_ERR_NONE)
                printf(" SYNC=\033[92m0x%02X\033[0m", (unsigned)sw);
            else
                printf(" SYNC=\033[91;5m0x%02X\033[0m", (unsigned)sw);
        } else {
            config_print_rejected("SYNC", val);
        }
    }

    if (key == "LDRO") {
        std::string norm = config_value_lower_ascii(config_value_trim_ascii(val));

        if (norm == "auto") {
            state = radio.autoLDRO();
            if (state == RADIOLIB_ERR_NONE)
                printf(" LDRO=\033[92mAUTO\033[0m");
            else
                printf(" LDRO=\033[91;5mAUTO\033[0m");
        } else {
            int ldro = 0;
            if (config_value_parse_bool01_exact(val, &ldro)) {
                state = radio.forceLDRO(ldro != 0);
                config_print_state_int("LDRO", ldro, state);
            } else {
                config_print_rejected("LDRO", val);
            }
        }
    }

    if (key == "POWER") {
        int p = 0;
        if (config_value_parse_int_exact(val, &p) && config_policy_power_valid(p)) {
            state = radio.setOutputPower(p);
            config_print_state_int("POWER", p, state);
        } else {
            config_print_rejected("POWER", val);
        }
    }

    fflush(stdout);
}

void apply_lora_param(SX1278 &radio, const char *tag, const std::string &key, const std::string &val)
{
    apply_lora_param_common(radio, tag, key, val);
}

void apply_lora_param(RFM95 &radio, const char *tag, const std::string &key, const std::string &val)
{
    apply_lora_param_common(radio, tag, key, val);
}

/* --- FSK CONFIG apply ---------------------------------------------------- */

template<typename RadioT>
static void apply_fsk_param_common(RadioT &radio,
                                   const char *tag,
                                   const std::string &key,
                                   const std::string &val)
{
    (void)tag;
    int state = 0;

    if (key == "FREQ") {
        float f = 0.0f;
        if (config_value_parse_float_exact(val, &f) && f > 0.0f) {
            state = radio.setFrequency(f);
            if (state == RADIOLIB_ERR_NONE)
                printf(" FREQ=\033[92m%.6f\033[0m", f);
            else
                printf(" FREQ=\033[91;5m%.6f\033[0m", f);
        } else {
            config_print_rejected("FREQ", val);
        }
    }

    if (key == "POWER") {
        int p = 0;
        if (config_value_parse_int_exact(val, &p) && config_policy_power_valid(p)) {
            state = radio.setOutputPower(p);
            config_print_state_int("POWER", p, state);
        } else {
            config_print_rejected("POWER", val);
        }
    }

    if (key == "BR") {
        float br = 0.0f;
        if (config_value_parse_float_exact(val, &br) && config_policy_fsk_bitrate_valid(br)) {
            state = radio.setBitRate(br);
            config_print_state_float("BR", br, state);
        } else {
            config_print_rejected("BR", val);
        }
    }

    if (key == "FREQDEV") {
        float fd = 0.0f;
        if (config_value_parse_float_exact(val, &fd) && config_policy_fsk_freqdev_valid(fd)) {
            state = radio.setFrequencyDeviation(fd);
            config_print_state_float("FREQDEV", fd, state);
        } else {
            config_print_rejected("FREQDEV", val);
        }
    }

    if (key == "RXBW") {
        float bw = 0.0f;
        if (config_value_parse_float_exact(val, &bw) && config_policy_fsk_rxbw_valid(bw)) {
            state = radio.setRxBandwidth(bw);
            config_print_state_float("RXBW", bw, state);
        } else {
            config_print_rejected("RXBW", val);
        }
    }

    if (key == "OOK") {
        int ook = 0;
        if (config_value_parse_bool01_exact(val, &ook)) {
            state = radio.setOOK(ook != 0);
            config_print_state_int("OOK", ook, state);
        } else {
            config_print_rejected("OOK", val);
        }
    }

    if (key == "SHAPING") {
        uint8_t sh = 0;
        const char *sh_label = NULL;
        if (parse_fsk_shaping(val, &sh, &sh_label)) {
            state = radio.setDataShaping(sh);
            if (state == RADIOLIB_ERR_NONE)
                printf(" SHAPING=\033[92m%s\033[0m", sh_label);
            else
                printf(" SHAPING=\033[91;5m%s\033[0m", sh_label);
        } else {
            config_print_rejected("SHAPING", val);
        }
    }

    if (key == "ENCODING") {
        int enc = 0;
        if (config_value_parse_int_exact(val, &enc) && enc >= 0 && enc <= 2) {
            state = radio.setEncoding((uint8_t)enc);
            config_print_state_int("ENCODING", enc, state);
        } else {
            config_print_rejected("ENCODING", val);
        }
    }

    if (key == "PREAMBLE") {
        int pre = 0;
        if (config_value_parse_int_exact(val, &pre) && config_policy_fsk_preamble_valid(pre)) {
            state = radio.setPreambleLength(pre);
            config_print_state_int("PREAMBLE", pre, state);
        } else {
            config_print_rejected("PREAMBLE", val);
        }
    }

    if (key == "SYNC") {
        uint32_t raw = 0;
        if (config_value_parse_hex_or_dec_u32_exact(val, &raw) && config_policy_fsk_sync_valid(raw)) {
            if (raw <= 0xFF) {
                uint8_t sw[1] = { (uint8_t)raw };
                state = radio.setSyncWord(sw, 1);
                if (state == RADIOLIB_ERR_NONE)
                    printf(" SYNC=\033[92m0x%02X\033[0m", (unsigned)sw[0]);
                else
                    printf(" SYNC=\033[91;5m0x%02X\033[0m", (unsigned)sw[0]);
            } else {
                uint8_t sw[2] = { (uint8_t)(raw >> 8), (uint8_t)(raw & 0xFF) };
                state = radio.setSyncWord(sw, 2);
                if (state == RADIOLIB_ERR_NONE)
                    printf(" SYNC=\033[92m0x%04X\033[0m", (unsigned)raw);
                else
                    printf(" SYNC=\033[91;5m0x%04X\033[0m", (unsigned)raw);
            }
        } else {
            config_print_rejected("SYNC", val);
        }
    }

    fflush(stdout);
}

void apply_fsk_param(SX1278 &radio, const char *tag, const std::string &key, const std::string &val)
{
    apply_fsk_param_common(radio, tag, key, val);
}

void apply_fsk_param(RFM95 &radio, const char *tag, const std::string &key, const std::string &val)
{
    apply_fsk_param_common(radio, tag, key, val);
}
