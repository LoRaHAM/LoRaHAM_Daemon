#include "sx127x_driver.h"

#include <stdio.h>

#include <chrono>
#include <thread>

#include "config_policy.h"
#include "driver_config_print.h"
#include "config_value.h"
#include "hardware_profile.h"

/* --- Construction ---------------------------------------------------------- */

Sx127xDriver::Sx127xDriver(Module *mod, bool is_hf, bool high_power)
    : RadioDriver(nullptr),
      mod_(mod),
      radio_(is_hf ? new RFM95(mod) : new SX1278(mod)),
      is_hf_(is_hf),
      high_power_(high_power)
{
    phy_ = radio_.get();
}

RadioDriver *sx127x_driver_create(Module *mod, bool is_hf, bool high_power)
{
    return new Sx127xDriver(mod, is_hf, high_power);
}

const char *Sx127xDriver::chipName() const
{
    return is_hf_ ? "RFM95" : "SX1278";
}

/* --- Boot init with the RF defaults ---------------------------------------- */
// The setter order is exactly that of the pre-driver initialisation
// (daemon_radio_init): frequency, SF, BW, sync word, preamble, CR, CRC, LDRO,
// power. LDRO < 0 means autoLDRO() only; >= 0 means forceLDRO(value).

int16_t Sx127xDriver::begin(const RadioRfDefaults *defaults)
{
    /* Fail closed: begin() without band defaults would leave the chip on
     * its register defaults (deaf on-band for an 868 process). */
    if (!defaults)
        return RADIOLIB_ERR_NULL_POINTER;

    int16_t state = radio_->begin();

    if (state != RADIOLIB_ERR_NONE)
        return state;

    /* Fail closed: every mandatory boot setter is checked. A
     * radio must never report READY with part of its RF configuration
     * rejected over SPI — the first failure is logged with its stage and
     * returned, and the caller marks the radio FAILED. */
    struct BootStep { const char *stage; int16_t state; } steps[] = {
        { "FREQ",     radio_->setFrequency(defaults->freq_mhz) },
        { "SF",       radio_->setSpreadingFactor(defaults->spreading_factor) },
        { "BW",       radio_->setBandwidth(defaults->bandwidth_khz) },
        { "SYNC",     radio_->setSyncWord(defaults->sync_word) },
        { "PREAMBLE", radio_->setPreambleLength(defaults->preamble_len) },
        { "CR",       radio_->setCodingRate(defaults->coding_rate) },
        { "CRC",      radio_->setCRC(defaults->crc_on) },
    };
    for (const BootStep &step : steps) {
        if (step.state != RADIOLIB_ERR_NONE) {
            printf("[sx127x] boot setter %s failed: %d\n",
                   step.stage, (int)step.state);
            fflush(stdout);
            return step.state;
        }
    }

    /* Always write LDRO to the register explicitly: autoLDRO() only sets the
     * cache flag, and RadioLib writes only on a cache difference. Without a
     * RESET line (Uputronics) a previously forced LDRO bit otherwise survives
     * the restart in the chip while the cache assumes the power-on state
     * (bench-verified: corrupt decoding at SF11/BW250).
     *
     * The SF/BW cache is seeded here, before the LDRO decision, because both
     * applyAutoLdro() and the CAD deadline read it. */
    sf_ = defaults->spreading_factor;
    bw_khz_ = defaults->bandwidth_khz;
    mode_ = RADIO_MODE_LORA;

    state = (defaults->ldro >= 0) ? radio_->forceLDRO(defaults->ldro != 0)
                                  : applyAutoLdro();
    if (state != RADIOLIB_ERR_NONE) {
        printf("[sx127x] boot setter LDRO failed: %d\n", (int)state);
        fflush(stdout);
        return state;
    }

    state = applyPowerAndOcp(defaults->power_dbm);
    if (state != RADIOLIB_ERR_NONE) {
        printf("[sx127x] boot setter POWER/OCP failed: %d\n",
               (int)state);
        fflush(stdout);
        return state;
    }

    return RADIOLIB_ERR_NONE;
}

/* --- LoRa <-> FSK modem switch --------------------------------------------- */

int16_t Sx127xDriver::switchMode(RadioMode_t mode,
                                 const RadioRfDefaults *defaults)
{
    /* Fail closed: mode switches without band defaults would park the chip
     * on its register defaults (deaf on-band for an 868 process). */
    if (!defaults)
        return RADIOLIB_ERR_NULL_POINTER;

    if (mode == RADIO_MODE_FSK) {
        /* FSK modem params stay the established legacy values (RadioLib
         * defaults: 4.8 kbps / 5 kHz dev / family RXBW raster) — only the
         * frequency comes from the band so the process never parks off-band. */
        int16_t state = radio_->beginFSK(defaults->freq_mhz);

        if (state != RADIOLIB_ERR_NONE)
            return state;

        mode_ = RADIO_MODE_FSK;

        /* beginFSK() re-pins OCP to RadioLib's 60 mA and resets the output
         * power, so without this every LoRa->FSK switch would silently undo
         * both. This is why power and OCP live in one helper called from three
         * places rather than being set once at boot. */
        return applyPowerAndOcp(defaults->power_dbm);
    }

    /* LoRa: land on the band boot defaults via the boot path (same setter
     * order incl. explicit LDRO write, which also covers the no-RESET
     * warm-start LDRO ghost). */
    return begin(defaults);
}

/* --- Live-RSSI (roher Registerzugriff) ------------------------------------ */
// Read live RSSI directly from the SX127x register.

float Sx127xDriver::readLiveRssi(RadioMode_t mode, bool is_hf)
{
    uint8_t reg = (mode == RADIO_MODE_LORA) ? 0x1B : 0x11;
    int16_t raw = mod_->SPIgetRegValue(reg, 7, 0);

    if (raw < 0)
        return -200.0f;

    if (mode == RADIO_MODE_LORA)
        return (is_hf ? -157.0f : -164.0f) + (float)raw;

    return -((float)raw) / 2.0f;
}

/* --- Non-destructive instant-RSSI probe ------------------------------------ */
// Live channel RSSI: packet=false reads the instant RSSI register (current
// channel energy, not the stale last-packet RSSI), skipReceive=true avoids
// re-entering RX. Non-destructive, same source as the GETRSSI live stream.

float Sx127xDriver::rssiProbe()
{
    return radio_->getRSSI(false, true);
}

/* --- LDRO ----------------------------------------------------------------- */

/* See the header for why autoLDRO() alone is not enough. */
int16_t Sx127xDriver::applyAutoLdro()
{
    int16_t state =
        radio_->forceLDRO(config_policy_lora_ldro_required(sf_, bw_khz_));

    if (state != RADIOLIB_ERR_NONE)
        return state;

    /* forceLDRO() clears ldroAuto, so this has to come after it -- and it is
     * what keeps LDRO correct through later SET SF / SET BW. */
    return radio_->autoLDRO();
}

/* --- Output power and PA over-current limit ------------------------------- */

/*
 * The two are one setting. See the header for why this exists and why it is
 * called from three places.
 *
 * Provenance, so a later reader does not have to guess which numbers are
 * datasheet and which are ours:
 *
 *   100 mA   the SX1276 silicon OCP default
 *    60 mA   what RadioLib pins it to, inside begin() AND beginFSK()
 *    87 mA   datasheet IDDT typical at +17 dBm on PA_BOOST
 *
 * The defect is that 60 mA sits BELOW the typical draw, so the protection can
 * trip during ordinary transmission. Restoring the silicon default fixes that
 * and asserts nothing of our own.
 *
 * An earlier version of this repair used 120 mA -- a margin we selected for
 * temperature and VSWR headroom, explicitly subject to a bench measurement.
 * That measurement needs a current meter on the PA supply, and no meter is
 * available for this work. Rather than ship an unmeasured number of our own
 * invention, the value is the chip's documented default: it needs no evidence
 * beyond the datasheet, and it leaves the part exactly as protected as an
 * unconfigured one. If a meter ever shows that +17 dBm into a real mismatch
 * needs more headroom, the number can rise -- with evidence behind it.
 *
 * setCurrentLimit accepts 45-240 mA; 100 mA is OcpTrim 11. The validator keeps
 * SX127x to 2..17 dBm -- plus exactly 20 under the --high-power permission --
 * so PA_BOOST is the only path this configures, and one value covers the
 * continuous range.
 *
 * +20 dBm is the exception, and the datasheet says so twice: "the Over
 * Current Protection limit should be adapted to the actual power level"
 * (5.4.3) and IDDT is 120 mA typical at +20 against 87 mA at +17 (2.5.1).
 * Imax bounds the PA current only, so the typical total does not translate
 * into an exact PA figure -- but 100 mA sits at or below the documented
 * +20 dBm region and is likely to limit the amplifier there, and RadioLib's
 * 60 mA certainly does. 140 mA (OcpTrim 17) is the value chosen for the mode:
 * above the documented operating region with headroom, the same pairing the
 * Arduino-LoRa library has shipped for years (140 above 17 dBm, 100 below).
 * It is a project choice, not a Semtech-prescribed number, and it has not
 * been current-measured on these boards (no meter, same as the 100). The
 * pair is written in one place: setOutputPower() flips RegPaDac to the
 * boosted byte (RadioLib does that for exactly 20 and un-boosts for anything
 * lower), then the limit follows the level, so leaving 20 -- SET lower, a
 * MODE switch that reloads the boot defaults, or a restart -- restores 100.
 */
#define SX127X_OCP_PA_BOOST_MA 100
#define SX127X_OCP_PA_BOOST_HP_MA 140

int16_t Sx127xDriver::applyPowerAndOcp(int power_dbm)
{
    int16_t state = radio_->setOutputPower((int8_t)power_dbm);

    if (state != RADIOLIB_ERR_NONE)
        return state;

    /* Order matters: power first, then the limit that protects it. A failure
     * here is returned, not logged and swallowed -- a transmitter running on
     * RadioLib's 60 mA cap is not a working radio. */
    state = radio_->setCurrentLimit(power_dbm == 20 ? SX127X_OCP_PA_BOOST_HP_MA
                                                    : SX127X_OCP_PA_BOOST_MA);
    if (state != RADIOLIB_ERR_NONE)
        return state;

    /* One concise line per successfully applied 20 -- both setters done, so a
     * rejected value or an SPI failure never produces it. The full contract
     * was printed once at startup (loraham_daemon.cpp). */
    if (power_dbm == 20) {
        printf("\n[sx127x] WARNING POWER=20 applied: +20 dBm, OCP %d mA; duty "
               "cycle <= 1 %%, operator responsible", SX127X_OCP_PA_BOOST_HP_MA);
    }

    return RADIOLIB_ERR_NONE;
}

/* --- Pending-RX, asked of the chip ----------------------------------------- */

/*
 * RxDone in RegIrqFlags is latched by the hardware the instant a packet
 * finishes arriving, so it is true even when the alert thread has not been
 * scheduled yet. That is what makes it the right question to ask immediately
 * before a probe destroys the receive state.
 *
 * FSK is not covered: the flag lives in different registers there and this
 * daemon's CAD is LoRa-only, so a probe never reaches the transition in FSK.
 */
bool Sx127xDriver::rxDonePending()
{
    if (mode_ != RADIO_MODE_LORA)
        return false;

    return (radio_->getIRQFlags() &
            RADIOLIB_SX127X_CLEAR_IRQ_FLAG_RX_DONE) != 0;
}

/* --- Register-polled CAD --------------------------------------------------- */

/*
 * Datasheet SX1276/77/78/79 p.44. The receiver-active part of a CAD is
 *
 *     t_receive = (2^SF + 32) / BW
 *
 * but that is not the whole operation: "The radio receiver and the PLL turn
 * off, and the modem digital processing starts... This correlation process
 * takes a little bit less than a symbol period to perform", and only THEN is
 * CadDone asserted. So the physical upper bound is the receive phase plus
 * roughly one more symbol period:
 *
 *     t_process      = 2^SF / BW
 *     physical_upper = t_receive + t_process
 *     deadline       = 2 x physical_upper, floor 10 ms
 *
 * The factor of two and the floor are an engineering choice, not a datasheet
 * figure: the deadline exists to stop an indefinite wait on a wedged chip, not
 * to time the operation, so it sits clear of the physical bound. The floor
 * matters at the fast end, where 2x is under a millisecond and any scheduling
 * hiccup would look like a hardware fault.
 *
 * Worked ends: SF7/500 kHz -> 0.58 ms upper, so the 10 ms floor applies.
 * SF12/7.8 kHz -> 1.05 s upper, deadline about 2.1 s.
 */
std::chrono::microseconds Sx127xDriver::cadDeadline() const
{
    const double symbols = (double)(1u << sf_);
    const double bw_hz = (double)bw_khz_ * 1000.0;

    if (!(bw_hz > 0.0))   /* never divide by a value a bad cache could hold */
        return std::chrono::milliseconds(10);

    const double upper_s = (symbols + 32.0) / bw_hz + symbols / bw_hz;
    double deadline_us = 2.0 * upper_s * 1e6;

    if (deadline_us < 10000.0)
        deadline_us = 10000.0;

    return std::chrono::microseconds((long long)deadline_us);
}

int16_t Sx127xDriver::scanChannel()
{
    /* startChannelScan() checks the active modem itself (ERR_WRONG_MODEM),
     * puts the chip in standby, clears the IRQ flags and enters CAD mode. */
    int16_t state = radio_->startChannelScan();

    if (state != RADIOLIB_ERR_NONE)
        return state;

    /* steady_clock, not RadioLib's hal->micros(): PiHal builds that from a
     * uint32_t and it wraps about every 72 minutes. */
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + cadDeadline();

    for (;;) {
        /* Read the flags BEFORE testing the deadline, every iteration: if
         * Linux descheduled us and the CAD completed meanwhile, that result
         * must be observed on wake, not turned into a manufactured timeout
         * because the scheduler was late. */
        const uint16_t flags = radio_->getIRQFlags();

        if (flags & RADIOLIB_SX127X_CLEAR_IRQ_FLAG_CAD_DONE) {
            /* Test CadDetected BEFORE clearing. Both bits come from the one
             * read above, so there is no window between them. */
            const bool detected =
                (flags & RADIOLIB_SX127X_CLEAR_IRQ_FLAG_CAD_DETECTED) != 0;

            radio_->clearIrqFlags(RADIOLIB_SX127X_CLEAR_IRQ_FLAG_CAD_DONE |
                                  RADIOLIB_SX127X_CLEAR_IRQ_FLAG_CAD_DETECTED);

            return detected ? RADIOLIB_PREAMBLE_DETECTED
                            : RADIOLIB_CHANNEL_FREE;
        }

        if (std::chrono::steady_clock::now() >= deadline) {
            /* Leave CAD mode rather than abandon the chip in it. The verdict
             * is indeterminate, never FREE and never BUSY. */
            radio_->standby();
            return RADIOLIB_ERR_RX_TIMEOUT;
        }

        /* A real sleep. hal->yield() is sched_yield(), not a sleep: on the slow
         * configurations it would hammer the CPU and the shared SPI bus, and
         * LockingPiHal takes a process-wide flock per transaction, so the 433
         * and 868 daemons would contend. 1 ms fixed -- the flags are latched,
         * so poll resolution costs latency, never correctness. */
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

/* --- D8 diagnosis for a failed begin() ------------------------------------- */
/*
 * The read goes through Module::SPIgetRegValue and thus inherits the SPI
 * flock and runs only after begin() already failed.
 */
void sx127x_diagnose_begin_failure(Module *mod, const char *band, int state)
{
    const DaemonHardwareProfile *hw = &daemon_hw_profile;
    char pins[96];

    snprintf(pins, sizeof(pins), "CS=%d DIO0=%d RST=%d DIO1=%d LED=%d",
             hw->cs, hw->irq, hw->rst, hw->gpio, hw->led_pin);

    if (state == RADIOLIB_ERR_CHIP_NOT_FOUND && mod) {
        int16_t ver = mod->SPIgetRegValue(0x42, 7, 0);

        if (ver <= 0 || ver == 0x00 || ver == 0xFF) {
            printf("[%s] diagnosis (profile %s): no answer on CS=BCM%d - module "
                   "missing, wrong CE switch or wrong profile; pins per "
                   "profile: %s (is another process holding one of these "
                   "lines?)\n",
                   band, hw->name, hw->cs, pins);
        } else {
            printf("[%s] diagnosis (profile %s): chip answers with ID 0x%02X "
                   "(expected 0x12) - wrong chip family or wrong profile; "
                   "pins per profile: %s\n",
                   band, hw->name, (unsigned)ver, pins);
        }
        return;
    }

    printf("[%s] diagnosis (profile %s): begin() error %d; pins per profile: "
           "%s (is another process holding one of these lines?)\n",
           band, hw->name, state, pins);
}



/* --- LoRa CONFIG apply --------------------------------------------------- */

int16_t Sx127xDriver::applyLoraParam(const char *tag,
                                  const std::string &key,
                                  const std::string &val)
{
    SX1278 &radio = *radio_;
    (void)tag;
    int state = 0;

    if (key == "SF") {
        int sf = 0;
        if (config_value_parse_int_exact(val, &sf) && config_policy_lora_sf_valid(sf)) {
            state = radio.setSpreadingFactor(sf);
            if (state == RADIOLIB_ERR_NONE)
                sf_ = sf;   /* only a written value describes the chip */
            driver_config_print_state_int("SF", sf, state);
        } else {
            driver_config_print_rejected("SF", val);
        }
    }

    if (key == "BW") {
        float bw = 0.0f;
        if (config_value_parse_float_exact(val, &bw) && config_policy_lora_bandwidth_valid(bw)) {
            state = radio.setBandwidth(bw);
            if (state == RADIOLIB_ERR_NONE)
                bw_khz_ = bw;   /* only a written value describes the chip */
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
            state = radio.setCRC(crc != 0);
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
            /* Not radio.autoLDRO(): that only sets a flag. The register has to
             * be written from the current SF/BW first, or a stale forced bit
             * survives a command that reported success. */
            state = applyAutoLdro();
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
        if (config_value_parse_int_exact(val, &p) &&
            config_policy_power_valid_family(p, DAEMON_CHIP_FAMILY_SX127X,
                                             high_power_)) {
            /* OCP with it, always: RadioLib's 60 mA cap would otherwise
             * survive every runtime power change. */
            state = applyPowerAndOcp(p);
            driver_config_print_state_int("POWER", p, state);
        } else {
            driver_config_print_rejected("POWER", val);
        }
    }

    fflush(stdout);
    return (int16_t)state;
}

/* --- FSK CONFIG apply ---------------------------------------------------- */

int16_t Sx127xDriver::applyFskParam(const char *tag,
                                 const std::string &key,
                                 const std::string &val)
{
    SX1278 &radio = *radio_;
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
        if (config_value_parse_int_exact(val, &p) &&
            config_policy_power_valid_family(p, DAEMON_CHIP_FAMILY_SX127X,
                                             high_power_)) {
            /* OCP with it, always: RadioLib's 60 mA cap would otherwise
             * survive every runtime power change. */
            state = applyPowerAndOcp(p);
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
        if (config_value_parse_float_exact(val, &bw) && config_policy_fsk_rxbw_valid(bw)) {
            state = radio.setRxBandwidth(bw);
            driver_config_print_state_float("RXBW", bw, state);
        } else {
            driver_config_print_rejected("RXBW", val);
        }
    }

    if (key == "OOK") {
        int ook = 0;
        if (config_value_parse_bool01_exact(val, &ook)) {
            state = radio.setOOK(ook != 0);
            driver_config_print_state_int("OOK", ook, state);
        } else {
            driver_config_print_rejected("OOK", val);
        }
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
