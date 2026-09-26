#include "daemon_led.h"

#include <stdio.h>

#include <lgpio.h>

#include "daemon_band.h"
#include "daemon_radio_runtime.h"
#include "hardware_profile.h"

/* --- LED/GPIO helpers ---------------------------------------------------- */
/*
 * The status LED is a per-band hardware resource (a GPIO line) and an activity
 * indicator -- it is NOT the process-instance ownership lock. Same-band
 * ownership and duplicate-instance rejection are handled by the per-band FD
 * locks in daemon_instance_lock (instance-433.lock / instance-868.lock).
 *
 * The LED line is claimed only for the single band this process serves, and a
 * failed claim is treated as fatal by the caller because the LED is a required
 * hardware resource for that band.
 */

static int daemon_led_chip = -1;
static int daemon_led_claimed = 0;

/* Profile-configurable LED line (legacy default set at configure time);
 * < 0 = LED disabled. */
static int daemon_led_pin = -1;
static int daemon_led_pin_set = 0;

void daemon_led_configure(int pin)
{
    daemon_led_pin = pin;
    daemon_led_pin_set = 1;
}

int daemon_led_pin_configured(void)
{
    if (!daemon_led_pin_set)
        return daemon_band()->legacy_led_pin;

    return daemon_led_pin;
}

static int daemon_led_claim_pin(int pin, int *claimed)
{
    int rc;

    if (!claimed)
        return -1;

    rc = lgGpioClaimOutput(daemon_led_chip, 0, pin, 0);

    if (rc < 0) {
        printf("[GPIO] error: LED GPIO %d could not be claimed: %d\n",
               pin, rc);
        return rc;
    }

    *claimed = 1;
    return 0;
}

static void daemon_led_release_pin(int pin, int *claimed)
{
    if (pin < 0 || daemon_led_chip < 0 || !claimed || !*claimed)
        return;

    lgGpioWrite(daemon_led_chip, pin, 0);
    lgGpioFree(daemon_led_chip, pin);
    *claimed = 0;
}

void daemon_led_shutdown(void)
{
    daemon_led_release_pin(daemon_led_pin_configured(), &daemon_led_claimed);

    if (daemon_led_chip >= 0) {
        lgGpiochipClose(daemon_led_chip);
        daemon_led_chip = -1;
    }
}

static int daemon_led_claim_band(int pin, int *claimed, const char *band)
{
    /* Profile without an LED line: feature cleanly disabled, not an error. */
    if (pin < 0) {
        printf("[GPIO] note: no LED GPIO in the hardware profile for band %s "
               "- LED disabled\n", band);
        return 0;
    }

    if (daemon_led_claim_pin(pin, claimed) < 0) {
        printf("[GPIO] error: LED GPIO %d (band %s, profile %s) could not be "
               "claimed - most likely another loraham or GPIO process holds "
               "this line\n",
               pin, band, daemon_hardware_preset_name());
        return -1;
    }

    return 0;
}

int daemon_led_init(void)
{
    int pin = daemon_led_pin_configured();

    daemon_led_shutdown();

    daemon_led_chip = lgGpiochipOpen(0);

    if (daemon_led_chip < 0) {
        printf("[GPIO] error: gpiochip0 could not be opened!\n");
        return -1;
    }

    /* Claim the LED line of this process's band only. */
    if (daemon_led_claim_band(pin, &daemon_led_claimed,
                              daemon_band()->tag) < 0) {
        daemon_led_shutdown();
        return -1;
    }

    daemon_led_set_pin(pin, 0);

    return 0;
}

int daemon_led_ready(void)
{
    if (daemon_led_chip < 0)
        return 0;

    /* The band must hold its LED line; a profile without an LED line
     * (pin < 0) counts as ready with the LED feature disabled. */
    if (daemon_led_pin_configured() >= 0 && !daemon_led_claimed)
        return 0;

    return 1;
}

void daemon_led_set_pin(int pin, int state)
{
    if (pin < 0)
        return;

    if (daemon_led_ready())
        lgGpioWrite(daemon_led_chip, pin, state ? 1 : 0);
}

/* --- Bodies moved verbatim from daemon_radio_runtime.h (D3; hosted here so LED logic links without the TX runtime) --- */

void daemon_radio_runtime_led(RadioController *ctrl,
                                            int state)
{
    if (!ctrl)
        return;

    daemon_led_set_pin(ctrl->led_pin, state);
}

void daemon_radio_runtime_sync_led(RadioController *ctrl)
{
    if (!ctrl)
        return;

    std::lock_guard<std::mutex> led_lock(ctrl->led_mutex);

    int desired = (ctrl->tx_busy.load() || ctrl->cad_broadcast_active.load())
                      ? 1 : 0;

    if (desired == ctrl->led_state)
        return;

    ctrl->led_state = desired;
    daemon_led_set_pin(ctrl->led_pin, desired);
}
