#ifndef LORAHAM_CONFIG_APPLY_H
#define LORAHAM_CONFIG_APPLY_H

#include "radio_channel.h"

/* --- CONFIG apply callback --- */

template<typename RadioT>
using ConfigApplyFn = void (*)(RadioT& radio,
                               const char *tag,
                               const char *cmd,
                               volatile RadioMode_t& mode,
                               volatile bool& getrssi_active);

template<typename RadioT>
void parse_and_apply_config_generic(RadioT &radio,
                                    const char *tag,
                                    const char *cmd,
                                    volatile RadioMode_t &mode_flag,
                                    volatile bool &getrssi_flag);

template<typename RadioT>
static void config_apply_command(RadioT& radio,
                                 const char *tag,
                                 const char *cmd,
                                 volatile RadioMode_t& mode,
                                 volatile bool& getrssi_active)
{
    parse_and_apply_config_generic<RadioT>(radio, tag, cmd, mode, getrssi_active);
}

#endif
