#include "config_policy.h"

#include <math.h>

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

/* Cap at 512 symbols: the old 65535 ceiling allowed a single
 * valid preamble of ~36 min at SF12/BW125. 512 covers every real profile
 * (boot uses 8/16); the merged effective-config airtime gate
 * (config_apply, CONFIG_POLICY_MAX_AIRTIME_MS) enforces the full
 * worst-case per-packet limit on top of this range. */
bool config_policy_lora_preamble_valid(int preamble)
{
    return preamble >= 6 && preamble <= 512;
}

bool config_policy_lora_sync_valid(uint32_t sync)
{
    return sync <= 0xFF;
}

bool config_policy_lora_ldro_required(int sf, float bw_khz)
{
    if (sf < 0 || bw_khz <= 0.0f)
        return false;

    /* Symbol time in ms: 2^SF / BW(Hz) * 1000. */
    const double t_sym_ms = (double)(1u << sf) / ((double)bw_khz * 1000.0) * 1000.0;

    return t_sym_ms >= 16.0;
}

bool config_policy_power_valid(int power)
{
    return power >= 0 && power <= 20;
}

/*
 * Output power is family-specific, in both directions.
 *
 * SX127x, low end: 0 and 1 dBm are not reachable on PA_BOOST at all. RadioLib's
 * SX1278::setOutputPower maps 2..17 to PA_BOOST and anything below 2 to the RFO
 * pin -- a different output path, which on these boards is not the one the
 * antenna is connected to. Accepting 0 or 1 therefore did not mean "transmit
 * quietly", it meant "transmit into an unconnected pin", and the caller was
 * told the setting succeeded.
 *
 * SX127x, 18 and 19: refused with or without permission. The pinned RadioLib
 * checkOutputPower accepts 2..17 on PA_BOOST and special-cases exactly 20, and
 * rejects 18 and 19 with ERR_INVALID_OUTPUT_POWER. That is an API boundary,
 * not a silicon one (Semtech's own reference driver reaches them through the
 * boosted PA with OutputPower = power - 5); this daemon does not go around the
 * library. Rejecting them here turns a late, opaque driver error into an
 * early, specific one.
 *
 * SX127x, 20: the datasheet's +20 dBm mode (RegPaDac 0x87, section 5.4.3),
 * restricted to a transmit duty cycle <= 1 %, VSWR <= 3:1 at the antenna port
 * and VDD 2.4-3.7 V. Nothing in this daemon measures or enforces any of that
 * -- there is no duty-cycle governor, by decision -- so 20 is admitted ONLY
 * when the operator started the process with --high-power, the explicit
 * acknowledgement of that contract. The permission is per process, immutable,
 * and no CONF command can grant it. The driver pairs the setting with the
 * over-current limit the mode needs (sx127x_driver.cpp). On the LoRaHAM board
 * the 433 module is an amplified RFM98PW whose documentation does not specify
 * this drive condition; the permission is the same switch there, and that
 * case is documented as unvalidated (docs/hardware.md).
 *
 * SX1262 keeps 0..20: its PA has a single output path and no such restriction,
 * so the permission changes nothing there.
 */
bool config_policy_power_valid_family(int power, DaemonChipFamily family,
                                      bool high_power)
{
    if (family == DAEMON_CHIP_FAMILY_SX127X) {
        if (power >= 2 && power <= 17)
            return true;
        return high_power && power == 20;
    }

    return config_policy_power_valid(power);
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

/* SX126x GFSK RX bandwidth raster (RadioLib SX126x::setRxBandwidth). The two
 * chip rasters barely intersect, so the validator must select per family. */
bool config_policy_fsk_rxbw_valid_sx126x(float bw)
{
    const float allowed[] = {
        4.8f, 5.8f, 7.3f, 9.7f, 11.7f, 14.6f,
        19.5f, 23.4f, 29.3f, 39.0f, 46.9f,
        58.6f, 78.2f, 93.8f, 117.3f, 156.2f,
        187.2f, 234.3f, 312.0f, 373.6f, 467.0f
    };

    for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++) {
        if (config_value_float_equal(bw, allowed[i]))
            return true;
    }

    return false;
}

bool config_policy_fsk_rxbw_valid_family(float bw, DaemonChipFamily family)
{
    if (family == DAEMON_CHIP_FAMILY_SX1262)
        return config_policy_fsk_rxbw_valid_sx126x(bw);

    return config_policy_fsk_rxbw_valid(bw);
}

bool config_policy_fsk_preamble_valid(int preamble)
{
    return preamble >= 0 && preamble <= 2048;
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

/* Band frequency policy: limits come from the immutable band
 * descriptor; validation receives them explicitly so this layer stays free
 * of daemon globals. */
bool config_policy_freq_valid_band(float freq_mhz,
                                   float min_mhz,
                                   float max_mhz)
{
    if (!(min_mhz > 0.0f) || !(max_mhz > min_mhz))
        return false;

    return freq_mhz >= min_mhz && freq_mhz <= max_mhz;
}

/* Family capability policy: prevalidation must reject what the
 * concrete driver will reject, or "whole-command prevalidation" is a lie
 * (SET MODE=FSK OOK=1 would switch mode and only then fail the key). */

bool config_policy_fsk_freqdev_valid_family(float freqdev,
                                            DaemonChipFamily family)
{
    if (!config_policy_fsk_freqdev_valid(freqdev))
        return false;

    /* SX126x rejects positive deviations below 0.6 kHz. */
    if (family == DAEMON_CHIP_FAMILY_SX1262)
        return freqdev >= 0.6f;

    return true;
}

bool config_policy_fsk_ook_valid_family(int ook, DaemonChipFamily family)
{
    /* SX126x has no OOK modulator: EVERY OOK key is rejected, including
     * OOK=0 — accepting a no-op setter would report OK for a capability
     * the chip does not have. */
    if (family == DAEMON_CHIP_FAMILY_SX1262)
        return false;

    return ook == 0 || ook == 1;
}

bool config_policy_fsk_encoding_valid_family(int encoding,
                                             DaemonChipFamily family)
{
    if (encoding < 0 || encoding > 2)
        return false;

    /* SX126x maps setEncoding to whitening only: 1 (Manchester) would
     * silently enable whitening instead — accept only 0 and 2. */
    if (family == DAEMON_CHIP_FAMILY_SX1262)
        return encoding != 1;

    return true;
}

/* Standard Semtech LoRa airtime (explicit header, worst case CRC on).
 * LDRO comes from config_policy_lora_ldro_required(), the one definition of
 * that boundary. */
double config_policy_lora_airtime_ms(int sf, float bw_khz, int cr,
                                     int preamble, size_t payload_len)
{
    if (sf < 6 || bw_khz <= 0.0f || cr < 5 || preamble < 0)
        return -1.0;

    double t_sym_ms = (double)(1u << sf) / ((double)bw_khz * 1000.0) * 1000.0;
    int de = config_policy_lora_ldro_required(sf, bw_khz) ? 1 : 0;
    double num = 8.0 * (double)payload_len - 4.0 * sf + 28.0 + 16.0;
    double den = 4.0 * (double)(sf - 2 * de);
    double payload_sym = 8.0;

    if (num > 0.0)
        payload_sym += ceil(num / den) * (double)cr;

    return ((double)preamble + 4.25 + payload_sym) * t_sym_ms;
}

/* FSK: preamble bits + payload/overhead bits at the configured bitrate. */
double config_policy_fsk_airtime_ms(float br_kbps, int preamble_bits,
                                    size_t payload_len)
{
    if (br_kbps <= 0.0f || preamble_bits < 0)
        return -1.0;

    double bits = (double)preamble_bits + 8.0 * ((double)payload_len + 10.0);

    return bits / ((double)br_kbps * 1000.0) * 1000.0;
}
