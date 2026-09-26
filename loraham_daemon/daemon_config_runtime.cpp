#include "daemon_config_runtime.h"

#include "client_slot.h"
#include "config_apply.h"
#include "daemon_band.h"
#include "daemon_high_power_boot.h"
#include "daemon_io_runtime.h"
#include "daemon_log.h"
#include "daemon_protocol.h"
#include "daemon_radio_runtime.h"

/* --- CONFIG apply, with the process permission ---------------------------- */

/* The ConfigApplyFn the dispatcher calls. This is the one place the immutable
 * boot permission enters the CONFIG path: no client can supply it, and the
 * prevalidation refuses POWER=20 without it before anything touches the radio. */
static ConfigApplyStatus config_apply_command(RadioDriver &radio,
                                              const char *tag,
                                              const char *cmd,
                                              RadioMode_t &mode,
                                              std::atomic<bool> &getrssi_active)
{
    return parse_and_apply_config_generic(radio, tag, cmd, mode, getrssi_active,
                                          daemon_high_power_enabled());
}

/* --- CONFIG runtime context factory -------------------------------------- */

static void daemon_config_trace_message(void *ctx, const char *msg)
{
    daemon_debug_ctx((const char *)ctx, "%s", msg);
}

static void daemon_config_trace_line(void *ctx,
                                     const char *msg,
                                     const char *line)
{
    daemon_debug_ctx((const char *)ctx, "%s: %s", msg, line ? line : "");
}

static ConfigDispatchLog daemon_config_log(const char *ctx)
{
    ConfigDispatchLog log = {
        (void *)ctx,
        daemon_config_trace_message,
        daemon_config_trace_line
    };

    return log;
}

ConfigDispatchContext daemon_config_context(void)
{
    const DaemonBandDescriptor *band = daemon_band();

    ConfigDispatchContext ctx = {
        client_conf_slots,
        &radio_controller,
        band->conf_log_ctx,
        config_apply_command,
        daemon_config_log(band->config_log_ctx)
    };

    return ctx;
}
