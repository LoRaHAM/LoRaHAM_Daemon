#ifndef LORAHAM_TEST_FAKE_SX127X_REGISTER_MODEL_H
#define LORAHAM_TEST_FAKE_SX127X_REGISTER_MODEL_H

/*
 * A register model of an SX127x, behind RadioLib's own HAL interface.
 *
 * A fake RadioDriver cannot catch the defects this repair is about: they live
 * in the real library and the real register sequence. So the tests compile the
 * REAL Sx127xDriver against the REAL pinned RadioLib, and put this model where
 * the chip would be. What RadioLib writes, this stores; what RadioLib reads, it
 * answers -- including the read-back verification RadioLib does after every
 * SPIsetRegValue, which a naive stub returning zeroes would fail.
 *
 * SPI framing (RadioLib's default SPIConfig, which SX127x uses unchanged):
 * byte 0 is the address, bit 7 set for a write; the remaining bytes are data,
 * and the address auto-increments except on the FIFO at 0x00.
 *
 * The CAD script is the point of the model. A real CAD asserts CadDone -- and,
 * on a successful correlation, CadDetected in the SAME register, per the
 * datasheet (p.44) -- some time after the chip enters CAD mode. The script says
 * after how many RegIrqFlags reads that happens and whether CadDetected comes
 * with it, or that it never happens at all, which is the wedged chip the
 * deadline exists for.
 */

#include <stdint.h>
#include <string.h>

#include <RadioLib.h>

class Sx127xRegisterModel : public RadioLibHal {
  public:
    /* --- register file --------------------------------------------------- */
    static const uint8_t REG_OP_MODE   = 0x01;
    static const uint8_t REG_IRQ_FLAGS = 0x12;
    static const uint8_t REG_VERSION   = 0x42;

    static const uint8_t FLAG_CAD_DETECTED = 0x01;
    static const uint8_t FLAG_CAD_DONE     = 0x04;
    static const uint8_t FLAG_TX_DONE      = 0x08;

    static const uint8_t MODE_MASK     = 0x07;
    static const uint8_t MODE_STANDBY  = 0x01;
    static const uint8_t MODE_CAD      = 0x07;
    static const uint8_t MODE_TX       = 0x03;
    static const uint8_t LONG_RANGE    = 0x80;

    /* --- CAD script ------------------------------------------------------ */
    /* How many RegIrqFlags reads happen before CadDone latches. 0 = already
     * done on the first read. Negative = never (the wedged chip). */
    int  cad_reads_until_done = 0;
    bool cad_detects = false;

    /* Transmission: true = TxDone asserts as soon as the chip enters TX.
     * false = it never does, which is what a dead DIO0 line looks like to
     * RadioLib's bounded wait. */
    bool tx_completes = true;

    /* --- observations ---------------------------------------------------- */
    int irq_flag_reads = 0;
    int cad_entries = 0;        /* transitions into CAD mode */
    int tx_entries = 0;         /* transitions into TX mode */
    int standby_entries = 0;    /* transitions into STANDBY */
    int irq_flag_clears = 0;
    uint8_t last_irq_clear_mask = 0;
    int spi_transfers = 0;
    int digital_reads = 0;      /* must stay 0: the repair reads registers */

    Sx127xRegisterModel()
      : RadioLibHal(/*input*/ 0, /*output*/ 1, /*low*/ 0, /*high*/ 1,
                    /*rising*/ 1, /*falling*/ 2)
    {
        reset();
    }

    void reset()
    {
        memset(regs_, 0, sizeof(regs_));
        regs_[REG_VERSION] = 0x12;                  /* or begin() reports
                                                     * CHIP_NOT_FOUND */
        regs_[REG_OP_MODE] = LONG_RANGE | MODE_STANDBY;
        cad_pending_ = false;
        cad_reads_left_ = 0;
        irq_flag_reads = 0;
        cad_entries = 0;
        tx_entries = 0;
        standby_entries = 0;
        irq_flag_clears = 0;
        last_irq_clear_mask = 0;
        spi_transfers = 0;
        digital_reads = 0;
    }

    uint8_t peek(uint8_t addr) const { return regs_[addr & 0x7F]; }
    void poke(uint8_t addr, uint8_t value) { regs_[addr & 0x7F] = value; }
    bool in_cad_mode() const
    {
        return (regs_[REG_OP_MODE] & MODE_MASK) == MODE_CAD;
    }

    /* --- RadioLibHal ----------------------------------------------------- */
    void pinMode(uint32_t, uint32_t) override { }
    void digitalWrite(uint32_t, uint32_t) override { }
    uint32_t digitalRead(uint32_t pin) override
    {
        /* Counted: register CAD must not consult a pin at all. The values are
         * here only so the stock RadioLib implementation can be exercised
         * against the same chip for comparison. */
        digital_reads++;

        if ((int)pin == dio0_pin && lora_modem()) {
            advance_cad();
            /* DIO0's meaning follows RegDioMapping1: CadDone during a scan,
             * TxDone during a transmission. Reading both is enough for a model
             * that never has two operations in flight. */
            return (regs_[REG_IRQ_FLAGS] & (FLAG_CAD_DONE | FLAG_TX_DONE))
                       ? 1 : 0;
        }

        if ((int)pin == dio1_pin && dio1_routed)
            return (regs_[REG_IRQ_FLAGS] & FLAG_CAD_DETECTED) ? 1 : 0;

        return 0;
    }
    /*
     * Pin view of the same chip, so the STOCK RadioLib scanChannel() can be run
     * against this model as a negative control. DIO0 carries CadDone; DIO1
     * carries CadDetected only where the board routes it, which the Uputronics
     * expansion board does not -- and that absence is what made every scan on
     * it report a free channel.
     */
    int dio0_pin = 25;
    int dio1_pin = 24;
    bool dio1_routed = false;

    /*
     * Write injection: writes to this address are dropped. RadioLib verifies
     * every SPIsetRegValue by reading the register back, so a dropped write
     * surfaces to the caller as a failed setter -- which is how a test can ask
     * what the driver does when the chip REJECTS a value rather than when the
     * value was rejected before the chip was touched. -1 disables it.
     */
    int reject_writes_to = -1;

    void attachInterrupt(uint32_t, void (*)(void), uint32_t) override { }
    void detachInterrupt(uint32_t) override { }
    void delay(RadioLibTime_t) override { }
    void delayMicroseconds(RadioLibTime_t) override { }
    /*
     * One monotonic clock, advanced by reading it. RadioLib's SPIsetRegValue
     * verifies a write by re-reading the register in a loop bounded by
     * hal->micros(), so a micros() that does not move turns a rejected write
     * into an infinite loop instead of the error the caller is owed.
     */
    RadioLibTime_t millis() override { clock_us_ += 1000; return clock_us_ / 1000; }
    RadioLibTime_t micros() override { clock_us_ += 100; return clock_us_; }
    long pulseIn(uint32_t, uint32_t, RadioLibTime_t) override { return 0; }

    void spiBegin() override { }
    void spiBeginTransaction() override { }
    void spiEndTransaction() override { }
    void spiEnd() override { }

    void spiTransfer(uint8_t *out, size_t len, uint8_t *in) override
    {
        spi_transfers++;
        if (len == 0)
            return;

        uint8_t addr = out[0] & 0x7F;
        const bool write = (out[0] & 0x80) != 0;

        if (in)
            in[0] = 0;

        for (size_t i = 1; i < len; i++) {
            if (write)
                write_reg(addr, out[i]);
            else if (in)
                in[i] = read_reg(addr);

            if (addr != 0x00)   /* FIFO does not auto-increment */
                addr++;
        }
    }

  private:
    /*
     * The register map is modem-dependent, and 0x12 is the trap: in LoRa it is
     * RegIrqFlags, write-1-to-clear, but in FSK the same address is an ordinary
     * configuration register that beginFSK() writes and RadioLib then verifies
     * by reading back. Treating it as write-1-to-clear in both modems made
     * beginFSK() fail with ERR_SPI_WRITE_FAILED against this model -- a model
     * bug that would otherwise have been read as a driver bug.
     */
    bool lora_modem() const { return (regs_[REG_OP_MODE] & LONG_RANGE) != 0; }

    uint8_t read_reg(uint8_t addr)
    {
        if (addr == REG_IRQ_FLAGS && lora_modem()) {
            irq_flag_reads++;
            advance_cad();
        }

        return regs_[addr];
    }

    void write_reg(uint8_t addr, uint8_t value)
    {
        if (addr == REG_IRQ_FLAGS && lora_modem()) {
            /* In LoRa, an IRQ flag is cleared by writing a 1 to it. */
            irq_flag_clears++;
            last_irq_clear_mask = value;
            regs_[addr] = (uint8_t)(regs_[addr] & ~value);
            return;
        }

        if ((int)addr == reject_writes_to)
            return;

        const uint8_t before = regs_[addr];
        regs_[addr] = value;

        if (addr == REG_OP_MODE) {
            const uint8_t was = before & MODE_MASK;
            const uint8_t now = value & MODE_MASK;

            if (now == MODE_TX && was != MODE_TX) {
                tx_entries++;
                if (tx_completes)
                    regs_[REG_IRQ_FLAGS] |= FLAG_TX_DONE;
            } else if (now == MODE_CAD && was != MODE_CAD) {
                cad_entries++;
                cad_pending_ = true;
                cad_reads_left_ = cad_reads_until_done;
            } else if (now == MODE_STANDBY && was != MODE_STANDBY) {
                standby_entries++;
            }
        }
    }

    /* Called on every RegIrqFlags read, BEFORE the value is returned, so a
     * script of 0 latches in time for the very first read. */
    void advance_cad()
    {
        if (!cad_pending_ || cad_reads_left_ < 0)
            return;

        if (cad_reads_left_ > 0) {
            cad_reads_left_--;
            return;
        }

        regs_[REG_IRQ_FLAGS] |= FLAG_CAD_DONE;
        if (cad_detects)
            regs_[REG_IRQ_FLAGS] |= FLAG_CAD_DETECTED;
        cad_pending_ = false;
    }

    uint8_t regs_[0x80];
    bool cad_pending_ = false;
    int cad_reads_left_ = 0;
    RadioLibTime_t clock_us_ = 0;
};

#endif
