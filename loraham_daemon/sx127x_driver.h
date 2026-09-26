#ifndef LORAHAM_SX127X_DRIVER_H
#define LORAHAM_SX127X_DRIVER_H

#include <chrono>
#include <memory>

#include <RadioLib.h>

#include "radio_driver.h"

/* --- SX127x driver family -------------------------------------------------- */
/*
 * The concrete RadioDriver for the SX127x family (SX1278 on 433, RFM95 on
 * 868). In RadioLib, RFM95 is an SX1276 alias and inherits from SX1278; the
 * object is held as an SX1278* and every chip-specific method (begin,
 * beginFSK, setFrequency, ...) is virtual and dispatches correctly.
 *
 * Every SX127x register constant (RegRssiValue 0x1B, RegRssiValueFSK 0x11,
 * RegVersion 0x42) lives exclusively in sx127x_driver.cpp.
 */

class Sx127xDriver : public RadioDriver {
public:
    /* high_power: the process's --high-power permission. Defence in depth only
     * -- the whole-command prevalidation has already refused a POWER=20 the
     * process may not apply -- so a driver built without it (the default,
     * every test rig) simply keeps 2..17. */
    Sx127xDriver(Module *mod, bool is_hf, bool high_power = false);

    int16_t begin(const RadioRfDefaults *defaults) override;
    int16_t switchMode(RadioMode_t mode,
                       const RadioRfDefaults *defaults) override;
    int16_t applyLoraParam(const char *tag, const std::string &key,
                        const std::string &val) override;
    int16_t applyFskParam(const char *tag, const std::string &key,
                       const std::string &val) override;
    float readLiveRssi(RadioMode_t mode, bool is_hf) override;
    float rssiProbe() override;

    /*
     * Register-polled CAD, replacing RadioLib's SX127x::scanChannel().
     *
     * RadioLib's version waits on `while(!digitalRead(DIO0))` and polls DIO1
     * for the detection, so a board that does not route DIO1 -- the Uputronics
     * expansion board -- can never observe CadDetected and reports CHANNEL_FREE
     * for every scan, including a busy channel. The datasheet (SX1276/77/78/79,
     * p.44) states that CadDone and CadDetected are asserted TOGETHER on a
     * successful correlation, and both are latched in RegIrqFlags -- so one
     * register read yields the complete verdict, with no ordering question and
     * no wired interrupt line. This is what RadioLib itself does on SX126x and
     * LR11x0; SX127x is the sole outlier.
     *
     * Returns RADIOLIB_CHANNEL_FREE, RADIOLIB_PREAMBLE_DETECTED (the same value
     * the blocking implementation returned when DIO1 fired), or a negative
     * RadioLib error. A hardware-deadline expiry is RADIOLIB_ERR_RX_TIMEOUT,
     * which the daemon maps to UNAVAILABLE: an indeterminate scan ends the TX
     * attempt and must never be flattened into BUSY, or CADTXAFTERTIMEOUT would
     * let a sequence of broken scans reach send-anyway.
     */
    int16_t scanChannel() override;

    /* Reads RegIrqFlags and reports RxDone. See RadioDriver::rxDonePending()
     * for why a probe must ask the chip and not only the software flag. */
    bool rxDonePending() override;
    const char *chipName() const override;
    DaemonChipFamily chipFamily() const override
    {
        return DAEMON_CHIP_FAMILY_SX127X;
    }

private:
    /*
     * Output power and the PA over-current limit are ONE transmitter setting,
     * applied together and never apart.
     *
     * RadioLib pins OCP to 60 mA inside both begin() and beginFSK(), which is
     * below the datasheet typical draw of 87 mA at +17 dBm on PA_BOOST -- so
     * the protection can trip during ordinary transmission, and the daemon
     * never set it. Because beginFSK() re-pins it, fixing boot and runtime
     * alone would reintroduce the fault on every LoRa/FSK switch; this helper
     * is therefore called from begin(), from switchMode(), and from SET POWER.
     *
     * The value is 100 mA, the chip's own silicon default. An earlier revision
     * used 120 mA as a project-selected margin for temperature and VSWR,
     * conditional on a bench measurement; no current meter was available, so
     * the default is used rather than an unmeasured number of our own. See
     * SX127X_OCP_PA_BOOST_MA in the .cpp for the full provenance.
     */
    int16_t applyPowerAndOcp(int power_dbm);

    /*
     * LDRO=AUTO, written to the register rather than only remembered.
     *
     * RadioLib's autoLDRO() sets a flag and writes NOTHING, and RadioLib only
     * writes on a cache difference -- so on a board without a RESET line
     * (Uputronics) a previously forced LDRO bit survives the restart in the
     * chip while the cache assumes the power-on state. The boot path already
     * worked around this, with a comment naming a bench-verified corrupt
     * decode at SF11/BW250; the runtime SET LDRO=AUTO path did not, so the
     * same stale bit could survive a CONFIG command that reported success.
     *
     * The fix is the boot rule, used by both: compute from the CURRENT SF/BW,
     * forceLDRO(computed) so the register is definitely right NOW, then
     * autoLDRO() so RadioLib keeps maintaining it across later SF/BW changes.
     * Order matters -- forceLDRO() clears the auto flag permanently, so it must
     * come first.
     */
    int16_t applyAutoLdro();

    /*
     * Upper bound for one CAD, computed from the CURRENT SF/BW -- never a fixed
     * constant. The validator accepts SF 7-12 and BW 7.8-500 kHz, so the
     * physical bound spans roughly three orders of magnitude (0.58 ms to 1.05 s).
     */
    std::chrono::microseconds cadDeadline() const;

    Module *mod_;
    std::unique_ptr<SX1278> radio_;
    bool is_hf_;
    bool high_power_;   /* the process's --high-power permission (defence in depth) */

    /*
     * The CAD bound needs the SF/BW the chip is on NOW, not the boot defaults.
     * config_apply.cpp tracks the CONFIG shadow, but that belongs to that module
     * -- pushing it down here would tangle the layers. These two fields are the
     * driver's own truth instead: seeded from the boot/mode-switch defaults and
     * updated only after a SUCCESSFUL setter, so they describe the chip and not
     * an intent.
     *
     * The seed is the slowest configuration the validator accepts. An
     * over-long deadline only delays noticing a completed CAD -- the flag is
     * latched, so nothing is lost -- while an under-long one manufactures a
     * timeout out of a scan that was still running.
     */
    int sf_ = 12;
    float bw_khz_ = 7.8f;

    /* The modem the chip is on, tracked the same way and for the same reason
     * as sf_/bw_khz_: RadioLib's getActiveModem() is protected, and rxDonePending()
     * must know which register map RegIrqFlags belongs to. Set by begin() and
     * by switchMode(), the only two places that change it. */
    RadioMode_t mode_ = RADIO_MODE_LORA;
};

/* Factory: is_hf=false -> SX1278 ("SX1278"), is_hf=true -> RFM95 ("RFM95"). */
RadioDriver *sx127x_driver_create(Module *mod, bool is_hf, bool high_power);

/*
 * D8: exactly one profile-aware diagnostic line for a failed SX127x begin().
 * For CHIP_NOT_FOUND a raw RegVersion read (0x42) distinguishes "no answer"
 * from "answers with an unexpected ID". This is a logging heuristic only; the
 * authoritative gate stays the begin() status and the fail-closed health path.
 */
void sx127x_diagnose_begin_failure(Module *mod, const char *band, int state);

#endif
