#ifndef LORAHAM_DAEMON_GPIO_LOCK_H
#define LORAHAM_DAEMON_GPIO_LOCK_H

#include <stddef.h>

/* --- Cross-process GPIO ownership ---------------------------- */
/*
 * lgpio's claim API prints and swallows errors, so RadioLib cannot report a
 * pin conflict — two band processes could silently fight over a line. These
 * locks provide deterministic cross-process exclusion independent of that:
 * one advisory flock per pin (gpio<N>.lock in the trusted runtime lock dir),
 * acquired non-blocking in ascending pin order (deadlock-free) BEFORE any
 * lgpio claim. A held pin fails the boot closed. The kernel releases the
 * locks on process exit; release() exists for orderly shutdown.
 */

/* Acquire locks for all pins (negatives skipped, duplicates collapsed).
 * Returns false on any conflict or lock-dir failure — caller must not
 * proceed to hardware init. Already-acquired locks are released on failure. */
bool daemon_gpio_locks_acquire(const int *pins, size_t count);

void daemon_gpio_locks_release(void);

/* Startup ordering seam: acquire the full pin set FIRST, then
 * run hw_init (the LED/hardware hook). Return contract:
 *   DAEMON_GPIO_CLAIM_LOCK_FAILED  acquisition failed; hw_init NOT called
 *   DAEMON_GPIO_CLAIM_HW_FAILED    hw_init failed; locks released again
 *   0                              locks held and hw_init succeeded
 * Injectable hook keeps this testable without hardware. */
#define DAEMON_GPIO_CLAIM_LOCK_FAILED (-1)
#define DAEMON_GPIO_CLAIM_HW_FAILED   1

int daemon_gpio_locks_claim_then(const int *pins, size_t count,
                                 int (*hw_init)(void));

/* Number of currently held pin locks (test/diagnostic). */
size_t daemon_gpio_locks_held(void);

#endif
