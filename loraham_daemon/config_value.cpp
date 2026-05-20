#include "config_value.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>

/* --- String helpers ------------------------------------------------------ */

std::string config_value_trim_ascii(const std::string &value)
{
    size_t begin = 0;
    while (begin < value.size() && isspace((unsigned char)value[begin]))
        begin++;

    size_t end = value.size();
    while (end > begin && isspace((unsigned char)value[end - 1]))
        end--;

    return value.substr(begin, end - begin);
}

std::string config_value_lower_ascii(std::string value)
{
    for (size_t i = 0; i < value.size(); i++)
        value[i] = (char)tolower((unsigned char)value[i]);

    return value;
}

static bool config_value_only_trailing_space(const char *end)
{
    while (*end != '\0' && isspace((unsigned char)*end))
        end++;

    return *end == '\0';
}

/* --- Numeric parsing ----------------------------------------------------- */

bool config_value_parse_int_exact(const std::string &value, int *out)
{
    if (!out)
        return false;

    std::string trimmed = config_value_trim_ascii(value);
    if (trimmed.empty())
        return false;

    errno = 0;
    char *end = NULL;
    long parsed = strtol(trimmed.c_str(), &end, 10);

    if (end == trimmed.c_str() || errno == ERANGE)
        return false;

    if (!config_value_only_trailing_space(end))
        return false;

    if (parsed < INT_MIN || parsed > INT_MAX)
        return false;

    *out = (int)parsed;
    return true;
}

bool config_value_parse_uint32_exact(const std::string &value, uint32_t *out)
{
    if (!out)
        return false;

    std::string trimmed = config_value_trim_ascii(value);
    if (trimmed.empty() || trimmed[0] == '-' || trimmed[0] == '+')
        return false;

    errno = 0;
    char *end = NULL;
    unsigned long parsed = strtoul(trimmed.c_str(), &end, 10);

    if (end == trimmed.c_str() || errno == ERANGE)
        return false;

    if (!config_value_only_trailing_space(end))
        return false;

    if (parsed > UINT32_MAX)
        return false;

    *out = (uint32_t)parsed;
    return true;
}

bool config_value_parse_float_exact(const std::string &value, float *out)
{
    if (!out)
        return false;

    std::string trimmed = config_value_trim_ascii(value);
    if (trimmed.empty())
        return false;

    errno = 0;
    char *end = NULL;
    float parsed = strtof(trimmed.c_str(), &end);

    if (end == trimmed.c_str() || errno == ERANGE)
        return false;

    if (!config_value_only_trailing_space(end))
        return false;

    if (!isfinite(parsed))
        return false;

    *out = parsed;
    return true;
}

bool config_value_parse_bool01_exact(const std::string &value, int *out)
{
    int parsed = 0;

    if (!config_value_parse_int_exact(value, &parsed))
        return false;

    if (parsed != 0 && parsed != 1)
        return false;

    if (out)
        *out = parsed;

    return true;
}

bool config_value_parse_hex_or_dec_u32_exact(const std::string &value, uint32_t *out)
{
    if (!out)
        return false;

    std::string trimmed = config_value_trim_ascii(value);
    if (trimmed.empty() || trimmed[0] == '-' || trimmed[0] == '+')
        return false;

    int base = 10;
    if (trimmed.size() > 2 && trimmed[0] == '0' &&
        (trimmed[1] == 'x' || trimmed[1] == 'X')) {
        base = 16;
    }

    errno = 0;
    char *end = NULL;
    unsigned long parsed = strtoul(trimmed.c_str(), &end, base);

    if (end == trimmed.c_str() || errno == ERANGE)
        return false;

    if (!config_value_only_trailing_space(end))
        return false;

    if (parsed > UINT32_MAX)
        return false;

    *out = (uint32_t)parsed;
    return true;
}

/* --- Comparison helpers -------------------------------------------------- */

bool config_value_float_equal(float value, float expected)
{
    return fabsf(value - expected) < 0.0001f;
}
