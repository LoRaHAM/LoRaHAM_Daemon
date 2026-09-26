#ifndef LORAHAM_CONFIG_APPLY_H
#define LORAHAM_CONFIG_APPLY_H

#include "radio_channel.h"
#include <string>
#include <atomic>
#include <stdio.h>
#include "radio_driver.h"

/* --- CONFIG apply result ------------------------------------ */
/*
 * The dispatcher decides from this whether the radio was touched: only
 * APPLIED re-arms RX (callback + startReceive). NOOP/REJECTED never touch
 * IRQ/RX state, so a malformed CONF line can no longer destroy a received,
 * undrained packet. HW_ERROR means a RadioLib setter or mode switch failed
 * mid-apply — the radio configuration is suspect and the dispatcher fails
 * the radio closed (RADIO_HEALTH_FAILED).
 */
typedef enum {
    CONFIG_APPLY_OK_NO_RADIO = 0, /* accepted; nothing touched the radio */
    CONFIG_APPLY_APPLIED,         /* success; radio touched, RX must re-arm */
    CONFIG_APPLY_REJECTED_INVALID,   /* valid syntax, rejected by policy */
    CONFIG_APPLY_REJECTED_MALFORMED, /* malformed token / incomplete SET */
    CONFIG_APPLY_UNKNOWN,            /* unknown command or unknown key */
    CONFIG_APPLY_HW_ERROR            /* setter/mode failure; radio suspect */
} ConfigApplyStatus;

/* True for every status that leaves the radio untouched (no RX re-arm). */
static inline bool config_apply_status_untouched(ConfigApplyStatus st)
{
    return st != CONFIG_APPLY_APPLIED && st != CONFIG_APPLY_HW_ERROR;
}

/* --- CONFIG apply callback --- */

using ConfigApplyFn = ConfigApplyStatus (*)(RadioDriver &radio,
                                            const char *tag,
                                            const char *cmd,
                                            RadioMode_t &mode,
                                            std::atomic<bool> &getrssi_active);

/* --- CONFIG command apply ------------------------------------------------ */
// Apply MODE first, then GETRSSI, then mode-specific radio parameters.
// SET without MODE keeps the current mode for backward compatibility.
// The chip-specific parameter setters live in the RadioDriver implementation
// (applyLoraParam/applyFskParam/switchMode); this layer stays chip-agnostic.
static inline void config_apply_print_prefix(const char *tag, bool *printed)
{
    if (!*printed) {
        printf("[%s]", tag);
        *printed = true;
    }
}

/* Reset the effective-config shadow to lazy band defaults (tests / re-init). */
void config_apply_effective_reset(void);

/* high_power: the process's immutable --high-power permission, supplied by the
 * caller at this boundary (daemon_config_runtime.cpp passes the boot flag) and
 * handed to the whole-command prevalidation, so a POWER=20 without it is
 * refused before MODE, GETRSSI or any RF setter runs. No default: every caller
 * states the permission it holds. */
ConfigApplyStatus parse_and_apply_config_generic(RadioDriver &radio,
                                                 const char *tag,
                                                 const char *cmd,
                                                 RadioMode_t &mode_flag,
                                                 std::atomic<bool> &getrssi_flag,
                                                 bool high_power);

#endif
