#include "daemon_radio_runtime.h"
#include "daemon_high_power_boot.h"

#include <stdio.h>

#include "daemon_band.h"
#include "daemon_gpio_lock.h"
#include "daemon_log.h"
#include "hardware_profile.h"
#include "daemon_stats.h"
#include "daemon_tx_async_runtime.h"
#include "radio_health.h"

/* --- Radio controller state --------------------------------------------- */

RadioController radio_controller;

/* --- Radio controller setup --------------------------------------------- */

void daemon_radio_controller_init(void)
{
    const DaemonBandDescriptor *band = daemon_band();

    daemon_debug_ctx("RADIO", "initialising the controller");

    daemon_tx_async_runtime_init();

    radio_controller_init(&radio_controller,
                          band->band,
                          band->tag,
                          band->is_hf,
                          setFlag,
                          daemon_led_pin_configured());
    daemon_debug_band(band->tag, "controller ready");

    /* Hardware capability from the resolved profile. */
    radio_controller.cad_scan_available = daemon_hw_profile.cad_scan_available;
    /* STATUS witnesses: the running family and the boot permission. */
    radio_controller.chip_family = daemon_hw_profile.family;
    radio_controller.high_power_enabled = daemon_high_power_enabled();
}

/* --- Radio controller shutdown ------------------------------------------ */

static void radio_controller_shutdown(RadioController *ctrl)
{
    const char *tag;

    if (!ctrl)
        return;

    tag = radio_controller_tag(ctrl);
    daemon_debug_ctx(tag, "radio shutdown");

    if (ctrl->driver) {
        if (radio_controller_ready(ctrl)) {
            daemon_debug_band(tag, "callback off");
            ctrl->driver->clearPacketReceivedAction();
            daemon_debug_band(tag, "standby");
            ctrl->driver->standby();
            daemon_debug_band(tag, "clearing IRQ");
            ctrl->driver->clearIrq(0xFFFFFFFF);
        } else {
            daemon_debug_band(tag, "radio not ready");
        }

        daemon_debug_band(tag, "releasing radio");
        ctrl->driver.reset();
    } else {
        daemon_debug_band(tag, "no radio object");
    }

    daemon_debug_band(tag, "releasing module");
    ctrl->mod.reset();
    daemon_debug_band(tag, "releasing HAL");
    ctrl->hal.reset();

    ctrl->health = RADIO_HEALTH_UNINITIALIZED;
    ctrl->received.store(false);
    ctrl->rx_rearm_pending.store(false);
    ctrl->tx_busy.store(false);
    ctrl->cad_active.store(false);
    ctrl->getrssi_active.store(false);
    daemon_radio_stats_init(&ctrl->stats);
    daemon_debug_band(tag, "state reset");
}

void daemon_radio_shutdown_cleanup(void)
{
    daemon_tx_async_runtime_shutdown();

    daemon_debug_ctx("RADIO", "shutdown %s", daemon_band()->tag);
    radio_controller_shutdown(&radio_controller);

    daemon_led_shutdown();
    daemon_gpio_locks_release();
}

/* --- RX callback ---------------------------------------------------------- */

void setFlag(void)
{
    radio_controller.received.store(true);
}

/* --- Active radio state -------------------------------------------------- */

bool daemon_selected_radio_ready(void)
{
    return radio_controller_ready(&radio_controller);
}

void daemon_log_active_radios(void)
{
    if (radio_controller_ready(&radio_controller))
        printf("[Daemon] active radios: %s\n", daemon_band()->tag);
    else
        printf("[Daemon] active radios: none\n");
}
