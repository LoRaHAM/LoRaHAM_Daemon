#ifndef LORAHAM_SX1262_DRIVER_H
#define LORAHAM_SX1262_DRIVER_H

#include <memory>

#include <RadioLib.h>

#include "radio_driver.h"

/* --- SX1262 driver ---------------------------------------------------------- */
/*
 * The concrete RadioDriver for the SX126x family (Waveshare SX1262 LoRaWAN
 * Node HAT; the LF and HF variants are pin-identical and the band comes from
 * --radio).
 *
 * The chip differences from SX127x, encapsulated here:
 *  - TCXO is fed from DIO3: begin() and beginFSK() set the TCXO reference
 *    voltage from the hardware profile.
 *  - RX/TX switching: DIO2 as the RF switch (setDio2AsRfSwitch), with
 *    TXEN/ANT_SW optionally as GPIO through Module::setRfSwitchPins.
 *  - Sync word: RadioLib maps the SX127x byte (0x12/0x2B) through the
 *    compatibility control bits (setSyncWord(byte, 0x44)); on-air
 *    compatibility with SX127x peers is a bench item.
 *  - CRC: setCRC(len) -- LoRa CRC on == 2 bytes, off == 0.
 *  - Power range -9..+22 dBm (RadioLib validates; the CONF policy stays
 *    0..20 and therefore inside the chip's range).
 *  - No OOK, and the FSK RXBW raster differs from the SX127x (RadioLib
 *    validates the chip's own raster and rejects foreign values).
 *  - Live RSSI through the GetRssiInst command (RadioLib getRSSI(false)),
 *    never through SX127x register addresses.
 */

class Sx1262Driver : public RadioDriver {
public:
    Sx1262Driver(Module *mod, float tcxo_voltage, int txen_pin);

    int16_t begin(const RadioRfDefaults *defaults) override;
    int16_t switchMode(RadioMode_t mode,
                       const RadioRfDefaults *defaults) override;
    int16_t applyLoraParam(const char *tag, const std::string &key,
                        const std::string &val) override;
    int16_t applyFskParam(const char *tag, const std::string &key,
                       const std::string &val) override;
    float readLiveRssi(RadioMode_t mode, bool is_hf) override;
    float rssiProbe() override;

    /* Reads the SX126x IRQ status and reports RxDone. See
     * RadioDriver::rxDonePending(): only meaningful after standby(). */
    bool rxDonePending() override;
    const char *chipName() const override;
    DaemonChipFamily chipFamily() const override
    {
        return DAEMON_CHIP_FAMILY_SX1262;
    }

private:
    Module *mod_;
    std::unique_ptr<SX1262> radio_;
    float tcxo_voltage_;
    int txen_pin_;
};

RadioDriver *sx1262_driver_create(Module *mod, float tcxo_voltage,
                                  int txen_pin);

/*
 * D8 (SX126x): exactly one profile-aware diagnostic line for a failed
 * begin(). It relies on RadioLib's own chip verification (version string plus
 * the BUSY protocol) and issues no hand-rolled pre-begin commands.
 */
void sx1262_diagnose_begin_failure(const char *band, int state);

#endif
