#include "sx1262_driver.h"

#include <stdio.h>

#include "config_policy.h"
#include "driver_config_print.h"
#include "config_value.h"
#include "hardware_profile.h"

/* --- Construction ---------------------------------------------------------- */

Sx1262Driver::Sx1262Driver(Module *mod, float tcxo_voltage, int txen_pin)
    : RadioDriver(nullptr),
      mod_(mod),
      radio_(new SX1262(mod)),
      tcxo_voltage_(tcxo_voltage),
      txen_pin_(txen_pin)
{
    phy_ = radio_.get();
}

RadioDriver *sx1262_driver_create(Module *mod, float tcxo_voltage,
                                  int txen_pin)
{
    return new Sx1262Driver(mod, tcxo_voltage, txen_pin);
}

const char *Sx1262Driver::chipName() const
{
    return "SX1262";
}



/* --- RF switch wiring, applied after begin()/beginFSK() -------------------- */

static int16_t sx1262_apply_rf_switch(SX1262 *radio, Module *mod, int txen_pin)
{
    /* The HAT switches TX internally through DIO2. The GPIO line labelled
     * "TXEN" (BCM 6) must be LOW in TX and HIGH in RX according to the
     * Waveshare reference driver (LoRaRF SX126x.py) -- the inverse of its
     * label. Registered as rxEn, RadioLib drives exactly that pattern;
     * registered as txEn, the line blocks the antenna path while
     * transmitting (bench-verified: TX_RESULT OK, but nothing radiated).
     *
     * Checked: a failed DIO2 switch command means TX_RESULT OK
     * with zero radiated RF — that must fail the boot/mode-switch closed,
     * not be discarded. (setRfSwitchPins is void in RadioLib — nothing to
     * check there.) */
    int16_t state = radio->setDio2AsRfSwitch(true);
    if (state != RADIOLIB_ERR_NONE) {
        printf("[sx1262] RF switch (DIO2) failed: %d\n", (int)state);
        fflush(stdout);
        return state;
    }

    if (mod && txen_pin >= 0)
        mod->setRfSwitchPins((uint32_t)txen_pin, RADIOLIB_NC);

    return RADIOLIB_ERR_NONE;
}

/* --- Boot init with the RF defaults ---------------------------------------- */
// begin() applies frequency, SF, BW, CR, sync word, power, preamble and the
// TCXO voltage (DIO3) in one call; the RF switch, CRC and LDRO follow
// explicitly, as in the SX127x driver, so that the effective configuration is
// deterministic in the same way.

int16_t Sx1262Driver::begin(const RadioRfDefaults *defaults)
{
    /* Fail closed: begin() without band defaults would leave the chip on
     * its register defaults (deaf on-band for an 868 process). */
    if (!defaults)
        return RADIOLIB_ERR_NULL_POINTER;

    int16_t state = radio_->begin(defaults->freq_mhz,
                                  defaults->bandwidth_khz,
                                  (uint8_t)defaults->spreading_factor,
                                  (uint8_t)defaults->coding_rate,
                                  defaults->sync_word,
                                  (int8_t)defaults->power_dbm,
                                  (uint16_t)defaults->preamble_len,
                                  tcxo_voltage_,
                                  false);

    if (state != RADIOLIB_ERR_NONE)
        return state;

    state = sx1262_apply_rf_switch(radio_.get(), mod_, txen_pin_);
    if (state != RADIOLIB_ERR_NONE)
        return state;

    /* Fail closed: the post-begin() steps are part of the
     * mandatory boot configuration — check them like begin() itself. */
    state = radio_->setCRC(defaults->crc_on ? 2 : 0);
    if (state != RADIOLIB_ERR_NONE) {
        printf("[sx1262] boot setter CRC failed: %d\n", (int)state);
        fflush(stdout);
        return state;
    }

    state = radio_->autoLDRO();
    if (state == RADIOLIB_ERR_NONE && defaults->ldro >= 0)
        state = radio_->forceLDRO(defaults->ldro != 0);
    if (state != RADIOLIB_ERR_NONE) {
        printf("[sx1262] boot setter LDRO failed: %d\n", (int)state);
        fflush(stdout);
        return state;
    }

    return RADIOLIB_ERR_NONE;
}

/* --- LoRa <-> FSK modem switch --------------------------------------------- */

int16_t Sx1262Driver::switchMode(RadioMode_t mode,
                                 const RadioRfDefaults *defaults)
{
    int16_t state;

    /* Fail closed: mode switches without band defaults would park the chip
     * on its register defaults (deaf on-band for an 868 process). */
    if (!defaults)
        return RADIOLIB_ERR_NULL_POINTER;

    if (mode == RADIO_MODE_FSK) {
        /* FSK modem params stay the established legacy values; only the
         * frequency comes from the band. TCXO always applied. */
        state = radio_->beginFSK(defaults->freq_mhz, 4.8, 5.0, 156.2, 10, 16,
                                 tcxo_voltage_, false);
        if (state == RADIOLIB_ERR_NONE)
            state = sx1262_apply_rf_switch(radio_.get(), mod_, txen_pin_);
        return state;
    }

    /* LoRa: land on the band boot defaults via the boot path (TCXO, RF
     * switch, CRC, LDRO all re-applied exactly like at startup). */
    return begin(defaults);
}

/* --- LoRa CONFIG apply ------------------------------------------------------ */

int16_t Sx1262Driver::applyLoraParam(const char *tag,
                                  const std::string &key,
                                  const std::string &val)
{
    SX1262 &radio = *radio_;
    (void)tag;
    int state = 0;

    if (key == "SF") {
        int sf = 0;
        if (config_value_parse_int_exact(val, &sf) && config_policy_lora_sf_valid(sf)) {
            state = radio.setSpreadingFactor(sf);
            driver_config_print_state_int("SF", sf, state);
        } else {
            driver_config_print_rejected("SF", val);
        }
    }

    if (key == "BW") {
        float bw = 0.0f;
        if (config_value_parse_float_exact(val, &bw) && config_policy_lora_bandwidth_valid(bw)) {
            state = radio.setBandwidth(bw);
            driver_config_print_state_float("BW", bw, state);
        } else {
            driver_config_print_rejected("BW", val);
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
            driver_config_print_rejected("FREQ", val);
        }
    }

    if (key == "CR") {
        int cr = 0;
        if (config_value_parse_int_exact(val, &cr) && config_policy_lora_cr_valid(cr)) {
            state = radio.setCodingRate(cr);
            driver_config_print_state_int("CR", cr, state);
        } else {
            driver_config_print_rejected("CR", val);
        }
    }

    if (key == "CRC") {
        int crc = 0;
        if (config_value_parse_bool01_exact(val, &crc)) {
            /* SX126x: CRC length in bytes; on == 2 (CCITT, as the LoRa
             * standard uses). */
            state = radio.setCRC(crc != 0 ? 2 : 0);
            driver_config_print_state_int("CRC", crc, state);
        } else {
            driver_config_print_rejected("CRC", val);
        }
    }

    if (key == "PREAMBLE") {
        int pre = 0;
        if (config_value_parse_int_exact(val, &pre) && config_policy_lora_preamble_valid(pre)) {
            state = radio.setPreambleLength(pre);
            driver_config_print_state_int("PREAMBLE", pre, state);
        } else {
            driver_config_print_rejected("PREAMBLE", val);
        }
    }

    if (key == "SYNC") {
        uint32_t sw = 0;
        if (config_value_parse_hex_or_dec_u32_exact(val, &sw) && config_policy_lora_sync_valid(sw)) {
            /* RadioLib maps the SX127x compatibility byte (control bits
             * 0x44); on-air compatibility is a bench item. */
            state = radio.setSyncWord((uint8_t)sw);
            if (state == RADIOLIB_ERR_NONE)
                printf(" SYNC=\033[92m0x%02X\033[0m", (unsigned)sw);
            else
                printf(" SYNC=\033[91;5m0x%02X\033[0m", (unsigned)sw);
        } else {
            driver_config_print_rejected("SYNC", val);
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
                driver_config_print_state_int("LDRO", ldro, state);
            } else {
                driver_config_print_rejected("LDRO", val);
            }
        }
    }

    if (key == "POWER") {
        int p = 0;
        if (config_value_parse_int_exact(val, &p) && config_policy_power_valid(p)) {
            /* Chip range -9..+22 dBm; the CONF policy (0..20) lies inside
             * it, and RadioLib validates on top. */
            state = radio.setOutputPower((int8_t)p);
            driver_config_print_state_int("POWER", p, state);
        } else {
            driver_config_print_rejected("POWER", val);
        }
    }

    fflush(stdout);
    return (int16_t)state;
}

/* --- FSK CONFIG apply -------------------------------------------------------- */

int16_t Sx1262Driver::applyFskParam(const char *tag,
                                 const std::string &key,
                                 const std::string &val)
{
    SX1262 &radio = *radio_;
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
            driver_config_print_rejected("FREQ", val);
        }
    }

    if (key == "POWER") {
        int p = 0;
        if (config_value_parse_int_exact(val, &p) && config_policy_power_valid(p)) {
            state = radio.setOutputPower((int8_t)p);
            driver_config_print_state_int("POWER", p, state);
        } else {
            driver_config_print_rejected("POWER", val);
        }
    }

    if (key == "BR") {
        float br = 0.0f;
        if (config_value_parse_float_exact(val, &br) && config_policy_fsk_bitrate_valid(br)) {
            state = radio.setBitRate(br);
            driver_config_print_state_float("BR", br, state);
        } else {
            driver_config_print_rejected("BR", val);
        }
    }

    if (key == "FREQDEV") {
        float fd = 0.0f;
        if (config_value_parse_float_exact(val, &fd) && config_policy_fsk_freqdev_valid(fd)) {
            state = radio.setFrequencyDeviation(fd);
            driver_config_print_state_float("FREQDEV", fd, state);
        } else {
            driver_config_print_rejected("FREQDEV", val);
        }
    }

    if (key == "RXBW") {
        float bw = 0.0f;
        /* The SX126x RXBW raster differs from the SX127x; RadioLib
         * validates the chip's own raster and returns an error for foreign
         * values (a red state rather than a silent acceptance). */
        if (config_value_parse_float_exact(val, &bw) && bw > 0.0f) {
            state = radio.setRxBandwidth(bw);
            driver_config_print_state_float("RXBW", bw, state);
        } else {
            driver_config_print_rejected("RXBW", val);
        }
    }

    if (key == "OOK") {
        /* The SX126x has no OOK modulator: fail closed, rejected visibly.
         * Non-success state: prevalidation blocks every OOK
         * key for this family, but a direct driver call must never report
         * success for a missing capability. */
        driver_config_print_rejected("OOK", val);
        printf(" (SX1262: OOK unavailable)");
        state = RADIOLIB_ERR_INVALID_MODULATION;
    }

    if (key == "SHAPING") {
        uint8_t sh = 0;
        const char *sh_label = NULL;
        if (driver_parse_fsk_shaping(val, &sh, &sh_label)) {
            state = radio.setDataShaping(sh);
            if (state == RADIOLIB_ERR_NONE)
                printf(" SHAPING=\033[92m%s\033[0m", sh_label);
            else
                printf(" SHAPING=\033[91;5m%s\033[0m", sh_label);
        } else {
            driver_config_print_rejected("SHAPING", val);
        }
    }

    if (key == "ENCODING") {
        int enc = 0;
        if (config_value_parse_int_exact(val, &enc) && enc >= 0 && enc <= 2) {
            state = radio.setEncoding((uint8_t)enc);
            driver_config_print_state_int("ENCODING", enc, state);
        } else {
            driver_config_print_rejected("ENCODING", val);
        }
    }

    if (key == "PREAMBLE") {
        int pre = 0;
        if (config_value_parse_int_exact(val, &pre) && config_policy_fsk_preamble_valid(pre)) {
            state = radio.setPreambleLength(pre);
            driver_config_print_state_int("PREAMBLE", pre, state);
        } else {
            driver_config_print_rejected("PREAMBLE", val);
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
            driver_config_print_rejected("SYNC", val);
        }
    }

    fflush(stdout);
    return (int16_t)state;
}

/* --- Live RSSI (the GetRssiInst command) ----------------------------------- */
// SX126x: instantaneous channel RSSI through the RadioLib command, never
// through SX127x register addresses. Mode-independent, and is_hf is irrelevant
// here (there is no offset split as on the SX127x).

float Sx1262Driver::readLiveRssi(RadioMode_t mode, bool is_hf)
{
    (void)mode;
    (void)is_hf;

    return radio_->getRSSI(false);
}

/* --- Non-destructive instant-RSSI probe ------------------------------------- */

float Sx1262Driver::rssiProbe()
{
    return radio_->getRSSI(false);
}

/* --- Pending-RX, asked of the chip ----------------------------------------- */

/*
 * The SX126x latches RxDone in its IRQ status the moment a packet finishes
 * arriving, so it answers even when the alert thread has not run. The bit is
 * the same in LoRa and FSK on this chip, so no modem check is needed.
 *
 * This exists because the first version of the repair gave RadioDriver a
 * default of false and overrode it only on SX127x -- which left the Waveshare
 * profile, whose active CAD is enabled, running the unprotected path.
 */
bool Sx1262Driver::rxDonePending()
{
    return (radio_->getIrqFlags() & RADIOLIB_SX126X_IRQ_RX_DONE) != 0;
}

/* --- D8 diagnosis for a failed begin() -------------------------------------- */

void sx1262_diagnose_begin_failure(const char *band, int state)
{
    const DaemonHardwareProfile *hw = &daemon_hw_profile;
    char pins[96];

    snprintf(pins, sizeof(pins), "CS=%d DIO1=%d RST=%d BUSY=%d TXEN=%d",
             hw->cs, hw->irq, hw->rst, hw->gpio, hw->txen);

    if (state == RADIOLIB_ERR_CHIP_NOT_FOUND) {
        printf("[%s] diagnosis (profile %s): SX1262 does not answer (version "
               "string / BUSY verification failed) - HAT missing, wrong "
               "profile or wiring; pins per profile: %s (is another process "
               "holding one of these lines?)\n",
               band, hw->name, pins);
        return;
    }

    printf("[%s] diagnosis (profile %s): begin() error %d; pins per profile: "
           "%s (is another process holding one of these lines?)\n",
           band, hw->name, state, pins);
}
