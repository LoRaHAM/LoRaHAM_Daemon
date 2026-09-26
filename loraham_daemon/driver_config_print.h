#ifndef LORAHAM_DRIVER_CONFIG_PRINT_H
#define LORAHAM_DRIVER_CONFIG_PRINT_H

#include <stdio.h>
#include <stdint.h>
#include <string>

#include <RadioLib.h>

#include "config_value.h"

/* --- Output and parse helpers shared by the radio drivers ------------------ */
/*
 * The coloured per-key state output of a CONFIG apply (green = accepted,
 * blinking red = rejected) and the FSK SHAPING parser. Shared by every
 * concrete RadioDriver implementation so that the output format stays
 * byte-identical.
 */

static inline void driver_config_print_rejected(const char *key,
                                                const std::string &val)
{
    printf(" %s=\033[91;5m%s\033[0m", key, val.c_str());
}

static inline void driver_config_print_state_int(const char *key, int value,
                                                 int state)
{
    if (state == RADIOLIB_ERR_NONE)
        printf(" %s=\033[92m%d\033[0m", key, value);
    else
        printf(" %s=\033[91;5m%d\033[0m", key, value);
}

static inline void driver_config_print_state_float(const char *key,
                                                   float value, int state)
{
    if (state == RADIOLIB_ERR_NONE)
        printf(" %s=\033[92m%.3f\033[0m", key, value);
    else
        printf(" %s=\033[91;5m%.3f\033[0m", key, value);
}

/* FSK-SHAPING: "off"/"none"/0.0/0.3/0.5/0.7/1.0 -> RadioLib-Konstante. */
static inline bool driver_parse_fsk_shaping(const std::string &val,
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

#endif
