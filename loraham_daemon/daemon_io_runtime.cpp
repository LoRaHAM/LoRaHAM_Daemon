#include "daemon_io_runtime.h"

#include "daemon_gpio_lock.h"
#include "loraham_runtime.h"

#include <stdio.h>
#include <stdlib.h>

#include "daemon_band.h"
#include "daemon_instance_lock.h"
#include "daemon_led.h"
#include "daemon_lifecycle.h"
#include "daemon_log.h"
#include "daemon_radio_init.h"
#include "daemon_radio_runtime.h"
#include "framed_data_tx.h"
#include "hardware_profile.h"
#include "unix_socket.h"

/* --- Daemon I/O runtime state ------------------------------------------- */
/* Socket fd and client-slot ownership for the one band of this process. */
int data_fd = -1;
int data_framed_fd = -1;
int conf_fd = -1;

ClientSlot client_data_slots[MAX_CLIENTS];
ClientSlot client_data_framed_slots[MAX_CLIENTS];
FramedDataTxState client_framed_states[MAX_CLIENTS];
ClientSlot client_conf_slots[MAX_CLIENTS];

/* Channel descriptor of the band. */
RadioChannelIo channel;

/* --- Shared I/O cleanup -------------------------------------------------- */
static void daemon_io_close(void)
{
    const DaemonBandDescriptor *band = daemon_band();

    client_slot_close_all(client_data_slots, MAX_CLIENTS);
    client_slot_close_all(client_data_framed_slots, MAX_CLIENTS);
    client_slot_close_all(client_conf_slots, MAX_CLIENTS);
    close_unix_socket(&data_fd, band->data_socket);
    close_unix_socket(&data_framed_fd, band->framed_socket);
    close_unix_socket(&conf_fd, band->conf_socket);
}

/* --- Daemon I/O lifecycle ----------------------------------------------- */
void daemon_io_startup_cleanup(void)
{
    daemon_debug_ctx("LIFE", "startup cleanup");
    daemon_radio_shutdown_cleanup();

    daemon_io_close();

    /* GPIO pin locks after radio/LED teardown, instance lock last: sockets
     * and hardware must be gone before ownership is released. */
    daemon_gpio_locks_release();
    daemon_instance_lock_release();
}


void daemon_io_shutdown_cleanup(void)
{
    daemon_io_close();
}

void daemon_io_init(void)
{
    const DaemonBandDescriptor *band = daemon_band();

    /*
     * Take the per-band instance-ownership lock FIRST -- before sockets, GPIO,
     * SPI, or radio setup. This is the authoritative same-band ownership
     * barrier and is held (via an open descriptor) until after socket cleanup,
     * so a same-band restart cannot bind sockets while this instance is still
     * shutting down.
     */
    int lock_rc = daemon_instance_lock_acquire();
    if (lock_rc != 0)
        exit(lock_rc);

    /*
     * Install stop-signal handlers immediately after taking ownership and before
     * sockets/GPIO/SPI/radio setup, so a SIGTERM/SIGINT arriving during the rest
     * of startup triggers a clean lifecycle exit (the main loop observes the stop
     * flag) instead of the default-disposition kill (exit 143).
     */
    daemon_lifecycle_reset_stop();
    if (daemon_lifecycle_install_signal_handlers() != 0) {
        perror("sigaction");
        printf("[Daemon] signal handlers could not be installed, exiting.\n");
        daemon_instance_lock_release();
        exit(EXIT_FAILURE);
    }

    daemon_debug_ctx("CLIENT", "initialising slots");
    client_slot_init_all(client_data_slots, MAX_CLIENTS);
    client_slot_init_all(client_data_framed_slots, MAX_CLIENTS);
    framed_data_tx_state_init_all(client_framed_states, MAX_CLIENTS);
    client_slot_init_all(client_conf_slots, MAX_CLIENTS);

    daemon_debug_ctx("RADIO", "initialising channel I/O");
    radio_channel_io_init(&channel,
                          band->band,
                          band->data_socket,
                          band->framed_socket,
                          band->conf_socket,
                          &data_fd,
                          &data_framed_fd,
                          &conf_fd,
                          client_data_slots,
                          client_data_framed_slots,
                          client_conf_slots);

    /*
     * Claim the band's status LED (a hardware resource). Same-band ownership
     * was already secured above by the instance FD lock; this only initialises
     * the LED line. A failed claim is fatal because the LED is a required
     * hardware resource for the band.
     */
    /* GPIO ownership BEFORE the first GPIO access: every pin
     * this process will drive — the profile's claimed set plus the possibly
     * overridden LED line — is locked before daemon_led_init() touches
     * lgpio and before any SPI/Module/RadioLib access in lora_init(). */
    daemon_debug_ctx("GPIO", "acquiring pin locks");
    daemon_led_configure(daemon_hw_profile.led_pin);
    {
        int pins[DAEMON_HW_MAX_CLAIMED + 1];
        size_t n = 0;

        for (int i = 0; i < daemon_hw_profile.claimed_count; i++)
            pins[n++] = daemon_hw_profile.claimed[i];
        pins[n++] = daemon_led_pin_configured();

        int claim_rc = daemon_gpio_locks_claim_then(pins, n, daemon_led_init);
        if (claim_rc == DAEMON_GPIO_CLAIM_LOCK_FAILED) {
            /* Lock INFRASTRUCTURE failure: exit 4 — systemd must
             * not restart-spin on a held/unusable pin lock. */
            printf("[Daemon] GPIO locks unavailable, exiting "
                   "(fail-closed, exit %d).\n", LORAHAM_EXIT_LOCK_ERROR);
            daemon_instance_lock_release();
            exit(LORAHAM_EXIT_LOCK_ERROR);
        }
        if (claim_rc != 0) {
            /* daemon_led_init() can only fail two ways, and both are startup
             * GPIO prerequisites, not transient hardware: gpiochip0 cannot be
             * opened (wrong device, missing node, no permission) or the LED
             * line cannot be claimed (held by a process outside this daemon's
             * own pin locks). Every restart repeats the same failure, so this
             * exits 4 with the lock-infrastructure failures rather than 1. */
            printf("[Daemon] GPIO/LED setup failed, exiting "
                   "(fail-closed, exit %d).\n", LORAHAM_EXIT_LOCK_ERROR);
            daemon_instance_lock_release();
            exit(LORAHAM_EXIT_LOCK_ERROR);
        }
    }

    daemon_debug_ctx("SOCKET", "opening socket files");
    if (radio_channel_open_sockets(&channel) != 0) {
        perror(band->band == RADIO_BAND_433 ? "socket 433" : "socket 868");
        printf("[Daemon] socket setup %s failed, exiting.\n", band->tag);
        daemon_io_startup_cleanup();
        exit(EXIT_FAILURE);
    }

    daemon_radio_controller_init();

    daemon_debug_ctx("RADIO", "initialising RadioLib");
    lora_init();

    daemon_log_active_radios();
    if (!daemon_selected_radio_ready()) {
        printf("[Daemon] no selected radio is ready, exiting.\n");
        daemon_io_startup_cleanup();
        /* Lock-infrastructure boot failures (unusable spi0.lock, missing
         * pin locks) exit 4 — not restartable; genuine radio hardware
         * failures keep the restartable exit 1. */
        exit(daemon_radio_boot_lock_failed() ? LORAHAM_EXIT_LOCK_ERROR
                                             : EXIT_FAILURE);
    }
}


void daemon_io_sync_event_fds(EventLoopSet *event_set)
{
    if (!event_set)
        return;

    /* Check BEFORE begin: the failure early-return must not leave
     * a dangling reconcile epoch (begin without matching end). Registration
     * errors are terminal via event_loop_wait either way; this keeps the
     * local begin⇒end invariant unconditional. */
    if (event_loop_registration_failed(event_set))
        return;

    event_loop_reconcile_begin(event_set);

    radio_channel_reconcile_fds(&channel, event_set);

    event_loop_reconcile_end(event_set);
}
