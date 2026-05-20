#ifndef LORAHAM_CONFIG_VALUE_H
#define LORAHAM_CONFIG_VALUE_H

#include <stdint.h>
#include <string>

/* --- Strict CONFIG value parsing --- */

std::string config_value_trim_ascii(const std::string &value);
std::string config_value_lower_ascii(std::string value);

bool config_value_parse_int_exact(const std::string &value, int *out);
bool config_value_parse_uint32_exact(const std::string &value, uint32_t *out);
bool config_value_parse_float_exact(const std::string &value, float *out);
bool config_value_parse_bool01_exact(const std::string &value, int *out);
bool config_value_parse_hex_or_dec_u32_exact(const std::string &value, uint32_t *out);

bool config_value_float_equal(float value, float expected);

#endif
