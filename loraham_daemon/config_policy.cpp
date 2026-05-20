#include "config_policy.h"

#include <stddef.h>

#include "config_value.h"

/* --- LoRa CONFIG value policy ------------------------------------------- */

bool config_policy_lora_sf_valid(int sf)
{
    return sf >= 7 && sf <= 12;
}

bool config_policy_lora_bandwidth_valid(float bw)
{
    const float allowed[] = {
        7.8f, 10.4f, 15.6f, 20.8f, 31.25f,
        41.7f, 62.5f, 125.0f, 250.0f, 500.0f
    };

    for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++) {
        if (config_value_float_equal(bw, allowed[i]))
            return true;
    }

    return false;
}

bool config_policy_lora_cr_valid(int cr)
{
    return cr >= 5 && cr <= 8;
}

bool config_policy_lora_preamble_valid(int preamble)
{
    return preamble >= 6 && preamble <= 65535;
}

bool config_policy_lora_sync_valid(uint32_t sync)
{
    return sync <= 0xFF;
}

bool config_policy_power_valid(int power)
{
    return power >= 0 && power <= 20;
}

/* --- FSK CONFIG value policy -------------------------------------------- */

bool config_policy_fsk_bitrate_valid(float br)
{
    return br >= 0.5f && br <= 300.0f;
}

bool config_policy_fsk_freqdev_valid(float freqdev)
{
    return freqdev > 0.0f && freqdev <= 200.0f;
}

bool config_policy_fsk_rxbw_valid(float bw)
{
    const float allowed[] = {
        2.6f, 3.1f, 3.9f, 5.2f, 6.3f, 7.8f,
        10.4f, 12.5f, 15.6f, 20.8f, 25.0f,
        31.25f, 31.3f, 41.7f, 50.0f, 62.5f,
        83.3f, 100.0f, 125.0f, 166.7f,
        200.0f, 250.0f
    };

    for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++) {
        if (config_value_float_equal(bw, allowed[i]))
            return true;
    }

    return false;
}

bool config_policy_fsk_preamble_valid(int preamble)
{
    return preamble >= 0;
}

bool config_policy_fsk_sync_valid(uint32_t sync)
{
    if (sync == 0 || sync > 0xFFFF)
        return false;

    if (sync <= 0xFF)
        return sync != 0x00;

    return ((sync >> 8) & 0xFF) != 0x00 &&
           (sync & 0xFF) != 0x00;
}
