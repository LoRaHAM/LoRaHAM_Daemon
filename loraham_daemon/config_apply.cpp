#include "config_apply.h"

#include "radio_tx_limit.h"

#include "daemon_band.h"

#include <vector>
#include <utility>

#include <RadioLib.h>

#include "config_parser.h"
#include "config_policy.h"
#include "rf_packet.h"
#include "config_validate.h"
#include "radio_rf_defaults.h"
#include "config_value.h"

/* --- CONFIG command apply ------------------------------------------------ */
// Chip-agnostic apply pipeline: parse, validate, MODE via
// RadioDriver::switchMode(), GETRSSI, then per-key parameters via
// RadioDriver::applyLoraParam()/applyFskParam(). The colored per-key state
// output for the chip parameters comes from the driver.


/* --- Effective RF config shadow ------------------------------ */
/*
 * One process, one radio: the CONF apply pipeline is the only runtime
 * configuration path, so this module can track the effective airtime-relevant
 * parameters. Lazy-initialized from the band boot defaults (which ARE the
 * radio state until the first SET); reset to them on every successful MODE
 * switch; updated per key on successful apply. Used to validate the merged
 * (current + command) configuration against the airtime limit BEFORE any
 * hardware side effect.
 */
typedef struct {
    bool set;
    int sf;
    float bw_khz;
    int cr;
    int preamble;
    float fsk_br_kbps;
    int fsk_preamble_bits;
} ConfigApplyEffective;

static ConfigApplyEffective g_effective;

static void config_apply_effective_load_defaults(ConfigApplyEffective *e)
{
    const RadioRfDefaults *d = daemon_band()->rf_defaults;

    e->sf = d->spreading_factor;
    e->bw_khz = d->bandwidth_khz;
    e->cr = d->coding_rate;
    e->preamble = d->preamble_len;
    /* FSK modem baseline = the established legacy switchMode values. */
    e->fsk_br_kbps = 4.8f;
    e->fsk_preamble_bits = 16;
    e->set = true;
}

void config_apply_effective_reset(void)
{
    g_effective.set = false;
}

static ConfigApplyEffective *config_apply_effective(void)
{
    if (!g_effective.set)
        config_apply_effective_load_defaults(&g_effective);

    return &g_effective;
}

/* Merged worst-case airtime of the command's prospective configuration.
 * Returns false (reject) when it exceeds CONFIG_POLICY_MAX_AIRTIME_MS. */
static bool config_apply_airtime_ok(const ConfigCommand &parsed,
                                    RadioMode_t target_mode,
                                    DaemonChipFamily family,
                                    const char *tag)
{
    ConfigApplyEffective merged = *config_apply_effective();

    /* A MODE token lands on the band boot defaults first. */
    if (!parsed.mode.empty())
        config_apply_effective_load_defaults(&merged);

    for (const auto &kv : parsed.tokens) {
        const std::string &key = kv.first;
        const std::string &val = kv.second;

        if (key == "SF")
            config_value_parse_int_exact(val, &merged.sf);
        else if (key == "BW")
            config_value_parse_float_exact(val, &merged.bw_khz);
        else if (key == "CR")
            config_value_parse_int_exact(val, &merged.cr);
        else if (key == "PREAMBLE" && target_mode == RADIO_MODE_LORA)
            config_value_parse_int_exact(val, &merged.preamble);
        else if (key == "PREAMBLE")
            config_value_parse_int_exact(val, &merged.fsk_preamble_bits);
        else if (key == "BR")
            config_value_parse_float_exact(val, &merged.fsk_br_kbps);
    }

    /* Worst case means the largest packet the daemon can actually SEND in the
     * prospective mode, not the largest the buffers could hold. The pure form
     * of the rule is required here: `SET MODE=FSK ...` is validated while the
     * controller still reports LoRa, so asking the controller would judge the
     * command against the old 255-byte limit and let a configuration through
     * on airtime the daemon will never produce. */
    const size_t worst_case_len = radio_tx_payload_limit(family, target_mode);

    double airtime_ms = (target_mode == RADIO_MODE_LORA)
        ? config_policy_lora_airtime_ms(merged.sf, merged.bw_khz, merged.cr,
                                        merged.preamble,
                                        worst_case_len)
        : config_policy_fsk_airtime_ms(merged.fsk_br_kbps,
                                       merged.fsk_preamble_bits,
                                       worst_case_len);

    if (airtime_ms < 0.0 || airtime_ms > CONFIG_POLICY_MAX_AIRTIME_MS) {
        printf("[%s] CONFIG rejected: worst-case airtime %.0f ms > %.0f ms "
               "(SF%d/BW%.1f/CR%d/PRE%d, %zu B)\n",
               tag, airtime_ms, CONFIG_POLICY_MAX_AIRTIME_MS,
               merged.sf, (double)merged.bw_khz, merged.cr,
               target_mode == RADIO_MODE_LORA ? merged.preamble
                                              : merged.fsk_preamble_bits,
               worst_case_len);
        fflush(stdout);
        return false;
    }

    return true;
}

/* Shadow bookkeeping after a successful per-key hardware apply. */
static void config_apply_effective_note_key(RadioMode_t mode,
                                            const std::string &key,
                                            const std::string &val)
{
    ConfigApplyEffective *e = config_apply_effective();

    if (mode == RADIO_MODE_LORA) {
        if (key == "SF")
            config_value_parse_int_exact(val, &e->sf);
        else if (key == "BW")
            config_value_parse_float_exact(val, &e->bw_khz);
        else if (key == "CR")
            config_value_parse_int_exact(val, &e->cr);
        else if (key == "PREAMBLE")
            config_value_parse_int_exact(val, &e->preamble);
    } else {
        if (key == "BR")
            config_value_parse_float_exact(val, &e->fsk_br_kbps);
        else if (key == "PREAMBLE")
            config_value_parse_int_exact(val, &e->fsk_preamble_bits);
    }
}

ConfigApplyStatus parse_and_apply_config_generic(RadioDriver &radio,
                                                 const char *tag,
                                                 const char *cmd,
                                                 RadioMode_t &mode_flag,
                                                 std::atomic<bool> &getrssi_flag,
                                                 bool high_power) {
    ConfigCommand parsed = config_parse_command(cmd);

    if(!parsed.is_set) {
        printf("[%s] unknown command: %s\n", tag, parsed.text.c_str());
        return CONFIG_APPLY_UNKNOWN;
    }

    /* SET without a single parameter is an incomplete setter. */
    if(!parsed.has_params)
        return CONFIG_APPLY_REJECTED_MALFORMED;

    ConfigValidationResult validation;
    if(!config_validate_command(parsed, mode_flag, &validation,
                                radio.chipFamily(),
                                daemon_band()->freq_min_mhz,
                                daemon_band()->freq_max_mhz,
                                high_power)) {
        printf("[%s] CONFIG rejected: %s=%s (%s)\n",
               tag,
               validation.key.c_str(),
               validation.value.c_str(),
               validation.reason.c_str());
        if (validation.reason == "malformed token")
            return CONFIG_APPLY_REJECTED_MALFORMED;
        if (validation.reason == "unknown key")
            return CONFIG_APPLY_UNKNOWN;
        return CONFIG_APPLY_REJECTED_INVALID;
    }

    bool hardware_touched = false;

    /* Airtime gate: merged current+command worst case, checked
     * BEFORE any hardware side effect. */
    if (!config_apply_airtime_ok(parsed, validation.target_mode,
                                 radio.chipFamily(), tag))
        return CONFIG_APPLY_REJECTED_INVALID;

    bool printed = false;

    // First pass: collect MODE, GETRSSI and radio parameters.
    std::vector<std::pair<std::string,std::string>> tokens;
    std::vector<std::pair<std::string,std::string>> getrssi_tokens;
    std::string mode_val = parsed.mode;

    for(auto &kv : parsed.tokens) {
        const std::string &key = kv.first;

        if(key == "GETRSSI")
            getrssi_tokens.push_back(kv);
        else
            tokens.push_back(kv);
    }

    // Apply MODE before any mode-specific parameter.
    if(!mode_val.empty()) {
        config_apply_print_prefix(tag, &printed);

        if(mode_val == "FSK") {
            printf(" MODE=FSK -> beginFSK()");
            int state = radio.switchMode(RADIO_MODE_FSK,
                                         daemon_band()->rf_defaults);
            if(state == RADIOLIB_ERR_NONE) {
                mode_flag = RADIO_MODE_FSK;
                hardware_touched = true;
                config_apply_effective_load_defaults(&g_effective);
                printf(" \033[92mOK\033[0m");
            } else {
                printf(" \033[91;5mERROR:%d\033[0m ABORT\n", state);

                return CONFIG_APPLY_HW_ERROR;
            }
        } else if(mode_val == "LORA") {
            printf(" MODE=LORA -> begin()");
            int state = radio.switchMode(RADIO_MODE_LORA,
                                         daemon_band()->rf_defaults);
            if(state == RADIOLIB_ERR_NONE) {
                mode_flag = RADIO_MODE_LORA;
                hardware_touched = true;
                config_apply_effective_load_defaults(&g_effective);
                printf(" \033[92mOK\033[0m");
            } else {
                printf(" \033[91;5mERROR:%d\033[0m ABORT\n", state);

                return CONFIG_APPLY_HW_ERROR;
            }
        } else {
            printf(" MODE=\033[91;5m%s\033[0m (unknown, ignored)", mode_val.c_str());
        }
    }

    // Apply GETRSSI after a successful MODE switch.
    for(auto &kv : getrssi_tokens) {
        const std::string &val = kv.second;
        int v = 0;

        config_apply_print_prefix(tag, &printed);
        if(config_value_parse_bool01_exact(val, &v)) {
            getrssi_flag.store(v != 0);
            if(v == 1) printf(" GETRSSI=\033[92m1\033[0m");
            else       printf(" GETRSSI=\033[92m0\033[0m");
        } else {
            printf(" GETRSSI=\033[91;5m%s\033[0m", val.c_str());
        }
    }

    // Second pass: apply remaining parameters for the active mode.
    for(auto &kv : tokens) {
        const std::string &key = kv.first;
        const std::string &val = kv.second;

        if(mode_flag == RADIO_MODE_FSK) {
            // Ignore LoRa-only keys while in FSK mode.
            if(key=="SF" || key=="BW" || key=="CR" || key=="LDRO" || key=="CRC") {
                printf(" \033[93m%s=IGNORED(LoRa key in FSK mode)\033[0m", key.c_str());
            } else {
                int16_t state = radio.applyFskParam(tag, key, val);
                if (state != RADIOLIB_ERR_NONE) {
                    printf(" \033[91;5mABORT after %s (error %d)\033[0m\n",
                           key.c_str(), (int)state);
                    return CONFIG_APPLY_HW_ERROR;
                }
                hardware_touched = true;
                config_apply_effective_note_key(RADIO_MODE_FSK, key, val);
            }
        } else {
            // Ignore FSK-only keys while in LoRa mode.
            if(key=="BR" || key=="FREQDEV" || key=="RXBW" || key=="OOK" ||
                key=="SHAPING" || key=="ENCODING") {
                printf(" \033[93m%s=IGNORED(FSK key in LoRa mode, SET MODE=FSK missing)\033[0m", key.c_str());
                } else {
                    int16_t state = radio.applyLoraParam(tag, key, val);
                    if (state != RADIOLIB_ERR_NONE) {
                        printf(" \033[91;5mABORT after %s (error %d)\033[0m\n",
                               key.c_str(), (int)state);
                        return CONFIG_APPLY_HW_ERROR;
                    }
                    hardware_touched = true;
                    config_apply_effective_note_key(RADIO_MODE_LORA, key, val);
                }
        }
    }
    printf("\n");

    return hardware_touched ? CONFIG_APPLY_APPLIED : CONFIG_APPLY_OK_NO_RADIO;
}
