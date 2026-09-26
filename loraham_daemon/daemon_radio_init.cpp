#include "daemon_radio_init.h"

#include <stdio.h>

#include "hal/RPi/PiHal.h"
#include <RadioLib.h>

#include "locking_pihal.h"

#include "daemon_band.h"
#include "daemon_gpio_lock.h"
#include "daemon_high_power_boot.h"
#include "daemon_led.h"
#include "daemon_log.h"
#include "daemon_radio_runtime.h"
#include "daemon_rx_rearm.h"
#include "hardware_profile.h"
#include "radio_controller.h"
#include "radio_driver.h"
#include "radio_health.h"
#include "sx127x_driver.h"
#include "sx1262_driver.h"

/* --- Profile helpers ------------------------------------------------------ */

static uint32_t hw_pin_or_nc(int pin)
{
    return pin < 0 ? RADIOLIB_NC : (uint32_t)pin;
}

/* Driver selection by profile chip family (is_hf mirrors today's band split:
 * 433 -> SX1278, 868 -> RFM95 within the SX127x family). */
static RadioDriver *hw_driver_create(Module *mod, bool is_hf)
{
    if (daemon_hw_profile.family == DAEMON_CHIP_FAMILY_SX1262)
        return sx1262_driver_create(mod,
                                    daemon_hw_profile.tcxo_voltage,
                                    daemon_hw_profile.txen);

    return sx127x_driver_create(mod, is_hf, daemon_high_power_enabled());
}

/* Family-aware diagnosis dispatch (one line per failed radio). */
static void hw_diagnose_begin_failure(Module *mod, const char *band, int state)
{
    if (daemon_hw_profile.family == DAEMON_CHIP_FAMILY_SX1262) {
        sx1262_diagnose_begin_failure(band, state);
        return;
    }

    sx127x_diagnose_begin_failure(mod, band, state);
}

/* One warm-start note for wiring without a reset line: a daemon restart is a
 * warm start against whatever state the chip is in; recovery from a wedged
 * chip needs a power cycle. */
static void hw_log_reset_note(const char *band)
{
    if (daemon_hw_profile.reset_wired)
        return;

    printf("[%s] note: RESET not wired (profile %s) - warm start, the previous "
           "chip state may persist; recovery only by power cycle\n",
           band, daemon_hw_profile.name);
}

/* Boot RF defaults live in the band descriptor (daemon_band.cpp). */

static bool g_boot_lock_failed = false;

bool daemon_radio_boot_lock_failed(void)
{
    return g_boot_lock_failed;
}

/* --- Radio startup/init -------------------------------------------------- */
void lora_init(void) {
    const DaemonBandDescriptor *band = daemon_band();
    const char *tag = band->tag;

    g_boot_lock_failed = false;

    printf("[Init] starting LoRa receiver: radio=%s\n", tag);
    daemon_debug_ctx("RADIO", "radio init starting");

    radio_controller.health = RADIO_HEALTH_UNINITIALIZED;
    daemon_debug_ctx("RADIO", "health reset");

    /* GPIO ownership is acquired in daemon_io_init() BEFORE the LED claim;
     * this is only the invariant check — a radio boot
     * without held pin locks would bypass the conflict gate. */
    if (daemon_gpio_locks_held() == 0) {
        printf("[GPIO] error: no pin locks held - radio init aborted "
               "(fail-closed)\n");
        g_boot_lock_failed = true;
        radio_controller.health = RADIO_HEALTH_FAILED;
        return;
    }

    if (!daemon_led_ready()) {
        printf("[GPIO] error: LED/GPIO not ready!\n");
        daemon_debug_ctx("GPIO", "not ready");
        radio_controller.health = RADIO_HEALTH_FAILED;
        return;
    }

    daemon_radio_runtime_led(&radio_controller, 1);

    daemon_debug_band(tag, "creating objects");
    hw_log_reset_note(tag);
    radio_controller.hal.reset(new LockingPiHal(0));
    radio_controller.mod.reset(new Module(
        radio_controller.hal.get(),
        hw_pin_or_nc(daemon_hw_profile.cs),
        hw_pin_or_nc(daemon_hw_profile.irq),
        hw_pin_or_nc(daemon_hw_profile.rst),
        hw_pin_or_nc(daemon_hw_profile.gpio)));
    radio_controller.driver.reset(
        hw_driver_create(radio_controller.mod.get(), band->is_hf));

    daemon_debug_band(tag, "begin()");
    /* Fail closed: never call begin() (which drives SPI) unless the
     * process-shared SPI lock was established. */
    int state;
    if (static_cast<LockingPiHal *>(radio_controller.hal.get())
            ->spi_lock_ready()) {
        state = radio_controller.driver->begin(band->rf_defaults);
    } else {
        state = RADIOLIB_ERR_SPI_CMD_FAILED;
        g_boot_lock_failed = true;
        printf("[SPI] error: SPI lock for %s unavailable - "
               "begin() skipped\n", tag);
    }
    /*
     * READY = configured radio + usable IRQ path + armed RX.
     *
     * Each of those three steps can fail through a HAL method that returns
     * void, so each is followed by a check of the HAL's startup latch. The
     * latch is the ONLY channel a GPIO failure has: PiHal's pinMode(),
     * digitalWrite() and attachInterrupt() report nothing to the caller, and
     * before this the daemon could publish READY on a radio whose IRQ line was
     * never claimed -- silent deafness, indistinguishable from a quiet band.
     *
     * The HAL is marked OPERATIONAL only once all three have passed, and health
     * is published READY only after that. The order matters: from the moment
     * the HAL is operational a GPIO failure is radio-I/O integrity loss and
     * exits the restartable way, so publishing READY first would leave a window
     * in which a live, advertised radio still carried startup semantics.
     */
    LockingPiHal *hal = static_cast<LockingPiHal *>(radio_controller.hal.get());
    bool startup_ok = false;

    if (state != RADIOLIB_ERR_NONE) {
        printf("[%s] init FAILED: %d\n", tag, state);
        hw_diagnose_begin_failure(radio_controller.mod.get(), tag, state);
        daemon_debug_band(tag, "begin() error %d", state);
    } else if (!hal->gpio_startup_ok()) {
        printf("[%s] init FAILED: GPIO startup error in begin()\n", tag);
        daemon_debug_band(tag, "begin() GPIO startup error");
    } else {
        printf("[%s] init OK\n", tag);
        daemon_debug_ctx(tag, "radio configured");

        daemon_debug_band(tag, "LoRa defaults applied");
        radio_controller.driver->setPacketReceivedAction(setFlag); // Callback nutzen
        daemon_debug_band(tag, "callback installed");

        if (hal->gpio_startup_ok()) {
            startup_ok = true;
        } else {
            printf("[%s] IRQ path unusable (alert claim failed) "
                   "- no READY\n", tag);
            daemon_debug_band(tag, "alert claim failed");
        }
    }

    /* Not READY yet -- FAILED is the honest value until the RX is armed, and
     * it is what the skip message below reports. */
    radio_controller.health = RADIO_HEALTH_FAILED;

    daemon_radio_runtime_led(&radio_controller, 0);

    if (startup_ok) {
        daemon_debug_band(tag, "starting RX");

        /* daemon_rx_rearm_boot_result() keeps the boot policy in one place:
         * a radio that cannot enter RX is deaf and boot has no recovery
         * story, so it reports and marks FAILED. */
        if (!daemon_rx_rearm_boot_result(
                &radio_controller, radio_controller.driver->startReceive())) {
            startup_ok = false;
        } else if (!hal->gpio_startup_ok()) {
            /* startReceive() drives the DIO mapping and, on profiles with an
             * RF switch, GPIO -- so the latch is checked once more before the
             * radio is called ready. */
            printf("[%s] GPIO startup error while arming RX - no READY\n", tag);
            startup_ok = false;
        }
    } else {
        printf("[%s] RX not started: %s\n",
               tag, radio_health_name(radio_controller.health));
        daemon_debug_band(tag, "RX start skipped");
    }

    /* A startup GPIO failure is not a radio that deserves another try: the
     * wiring, the permissions or a busy line are the same after every restart.
     * Fold it into the same flag the unusable SPI lock uses, so
     * daemon_io_init() exits 4 (RestartPreventExitStatus) instead of 1, which
     * would restart-spin every two seconds forever. Checked here, after the
     * last startup GPIO access, so no failure above can be missed. */
    if (!hal->gpio_startup_ok())
        g_boot_lock_failed = true;

    if (startup_ok) {
        /* Operational BEFORE READY: from here a negative lgpio result is loss
         * of radio I/O integrity and exits 5, exactly as a failed SPI transfer
         * already does. */
        hal->gpio_mark_operational();
        radio_controller.health = RADIO_HEALTH_READY;
        daemon_debug_ctx(tag, "radio ready");
    }

    daemon_debug_ctx("RADIO", "radio init complete");
    fflush(stdout);
}
