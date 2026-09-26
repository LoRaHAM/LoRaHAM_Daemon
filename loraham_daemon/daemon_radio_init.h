#ifndef LORAHAM_DAEMON_RADIO_INIT_H
#define LORAHAM_DAEMON_RADIO_INIT_H

/* --- Radio startup/init -------------------------------------------------- */

void lora_init(void);

/* True when the radio boot failed because LOCK INFRASTRUCTURE (shared SPI
 * lock file, missing GPIO pin locks) was unusable — the caller exits with
 * LORAHAM_EXIT_LOCK_ERROR (4, not restartable) instead of the generic
 * hardware-failure exit 1. */
bool daemon_radio_boot_lock_failed(void);

#endif
