/******************************************************************************
 * Copyright (C) 2026  LoRaHAM / Alexander Walter
 * Copyright (C) 2026  Johannes Loose <410733@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <https://www.gnu.org/licenses/>.
 *****************************************************************************/
/*
 * Main runtime for the LoRaHAM radio daemon.
 *
 * Public interface, build notes, and examples live in README.md.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <getopt.h>
#include <errno.h>

#include <RadioLib.h>

#include "daemon_protocol.h"
#include "daemon_version.h"
#include "daemon_timing.h"
#include "daemon_stats.h"
#include "daemon_lifecycle.h"
#include "daemon_stdout_stamp.h"
#include "daemon_band.h"
#include "daemon_radio_selection.h"
#include "hardware_profile.h"
#include "daemon_tx_mode_boot.h"
#include "daemon_cad_monitor_boot.h"
#include "daemon_cad_rssi_boot.h"
#include "daemon_rflog.h"
#include "daemon_high_power_boot.h"
#include "daemon_radio_runtime.h"
#include "daemon_data_tx_runtime.h"
#include "daemon_log.h"
#include "daemon_rx.h"
#include "daemon_monitoring.h"
#include "daemon_io_runtime.h"
#include "daemon_instance_lock.h"
#include "event_loop.h"
#include "daemon_socket_dispatch.h"
#include "radio_controller.h"
#include "config_dispatch.h"
#include "daemon_config_runtime.h"

/* --- Shutdown cleanup ---------------------------------------------------- */
static void daemon_shutdown_cleanup(EventLoopSet *event_set)
{
    daemon_debug_ctx("LIFE", "stopping the radio modules");
    daemon_radio_shutdown_cleanup();

    daemon_rflog_stop();

    daemon_debug_ctx("LIFE", "closing the event backend");
    event_loop_close(event_set);

    daemon_debug_ctx("LIFE", "closing clients");
    daemon_io_shutdown_cleanup();

    daemon_debug_ctx("LIFE", "removing socket files");

    /* Release per-band ownership only after all sockets are closed/unlinked, so
     * a same-band restart cannot bind sockets that this instance then deletes. */
    daemon_debug_ctx("LIFE", "releasing the instance lock");
    daemon_instance_lock_release();
}

/* --- Event wait/runtime -------------------------------------------------- */
static int daemon_wait_for_events(EventLoopSet *event_set,
                                   EventLoopReadySet *readfds)
{
    int ret = event_loop_wait(event_set, readfds,
                              DAEMON_EVENT_LOOP_TIMEOUT_USEC);

    if (ret > 0)
        daemon_debug_ctx("SOCKET", "%d event(s)", ret);

    return ret;
}

static void daemon_runtime_init(EventLoopSet *event_set)
{
    // Initialize event backend.
    if (event_loop_init(event_set) != 0) {
        perror("epoll");
        printf("[Daemon] event backend could not be started, exiting.\n");
        daemon_shutdown_cleanup(event_set);
        exit(EXIT_FAILURE);
    }

    printf("[Daemon] event backend: %s\n",
           event_loop_backend_name(event_loop_backend(event_set)));

    /* Stop-signal handlers are installed earlier, in daemon_io_init(), right
     * after the instance lock is acquired (see M4-P2). */
}

/* --- Loop context --------------------------------------------------------- */
typedef struct {
    DaemonDeadlineTimer cad_timer;
    DaemonDeadlineTimer rssi_timer;
    DaemonDeadlineTimer stats_timer;
    DataTxDaemonContext data_tx_ctx;
    ConfigDispatchContext config_ctx;
} DaemonLoopContext;

static void daemon_loop_context_init(DaemonLoopContext *ctx)
{
    DaemonTimeMs now = daemon_now_ms();    // CAD monitoring timer.
    daemon_deadline_timer_init(&ctx->cad_timer,
                                now,
                                DAEMON_CAD_POLL_INTERVAL_MS);



    // RSSI timer.
    daemon_deadline_timer_init(&ctx->rssi_timer,
                               now,
                               DAEMON_RSSI_INTERVAL_MS);

    // Periodic operator stats.
    daemon_stats_start(now);
    daemon_deadline_timer_init(&ctx->stats_timer,
                               now,
                               DAEMON_STATS_LOG_INTERVAL_MS);

    // DATA TX context.
    ctx->data_tx_ctx = daemon_data_tx_context(&radio_controller);

    // CONFIG context (CONF slots are initialized in daemon_io_init).
    ctx->config_ctx = daemon_config_context();
}

/* --- Main runtime context ------------------------------------------------ */
typedef struct {
    EventLoopSet event_set;
    EventLoopReadySet readfds;
    uint8_t buf[buf_SIZE];     // CONF read scratch buffer.
    uint8_t rx_buf[buf_SIZE];  // RF RX buffer.
    DaemonLoopContext loop_ctx;
} DaemonMainContext;

static void daemon_main_context_init(DaemonMainContext *ctx)
{
    daemon_debug_ctx("LIFE", "initialising the runtime context");
    daemon_runtime_init(&ctx->event_set);
    daemon_loop_context_init(&ctx->loop_ctx);
    daemon_io_sync_event_fds(&ctx->event_set);
    daemon_debug_ctx("LIFE", "runtime context ready");
}

/* --- Main loop logging --------------------------------------------------- */
static void daemon_log_loop_start(void)
{
    printf("[Daemon] starting the polling loop for LoRa and sockets (radio=%s)\n",
           daemon_radio_selection_name(daemon_radio_selection));
}

/* --- Radio polling order ------------------------------------------------- */
static void daemon_process_radio_polling(DaemonDeadlineTimer *cad_timer,
                                          DaemonDeadlineTimer *rssi_timer,
                                          DaemonDeadlineTimer *stats_timer,
                                          uint8_t (&rx_buf)[buf_SIZE])
{
    daemon_process_radio(rx_buf);

    // Monitoring: CAD/RSSI/status stats.
    daemon_process_monitoring(cad_timer, rssi_timer, stats_timer);
}

/* --- Main loop iteration ------------------------------------------------- */
static void daemon_process_loop_iteration(EventLoopSet *event_set,
                                           EventLoopReadySet *readfds,
                                           DaemonLoopContext *loop_ctx,
                                           uint8_t *buf,
                                           uint8_t (&rx_buf)[buf_SIZE])
{
    // Wait for socket events.
    int ret = daemon_wait_for_events(event_set, readfds);
    if (ret < 0) {
        switch (daemon_lifecycle_classify_wait_error(
                    errno, daemon_lifecycle_stop_requested())) {
        case DAEMON_WAIT_ERROR_STOPPING:
            daemon_debug_ctx("LIFE", "event wait interrupted by stop");
            return;
        case DAEMON_WAIT_ERROR_SILENT:
            daemon_debug_ctx("LIFE",
                             "event wait interrupted by a signal (EINTR)");
            return;
        case DAEMON_WAIT_ERROR_LOG:
            break;
        }

        perror("event_loop_wait");

        if (event_loop_registration_failed(event_set))
            daemon_lifecycle_request_stop(0);

        return;
    }

    // Process ready socket clients.
    daemon_process_ready_sockets(&loop_ctx->config_ctx,
                                 &loop_ctx->data_tx_ctx,
                                 readfds, buf);

    daemon_process_radio_polling(&loop_ctx->cad_timer,
                                 &loop_ctx->rssi_timer,
                                 &loop_ctx->stats_timer,
                                 rx_buf);

    daemon_io_sync_event_fds(event_set);
}

/* --- Polling loop -------------------------------------------------------- */
static void daemon_run_polling_loop(DaemonMainContext *ctx)
{
    daemon_log_loop_start();
    daemon_debug_ctx("LIFE", "polling active");

    while (!daemon_lifecycle_stop_requested()) {
        daemon_process_loop_iteration(&ctx->event_set,
                                      &ctx->readfds,
                                      &ctx->loop_ctx,
                                      ctx->buf,
                                      ctx->rx_buf);
    }
}

/* --- Daemon run ---------------------------------------------------------- */
static void daemon_run(void)
{
    DaemonMainContext main_ctx;
    daemon_main_context_init(&main_ctx);

    daemon_run_polling_loop(&main_ctx);

    daemon_log("stop requested");
    daemon_debug_ctx("LIFE", "shutdown starting");
    daemon_shutdown_cleanup(&main_ctx.event_set);
    daemon_debug_ctx("LIFE", "shutdown complete");
    /* A line that was built but never terminated would otherwise be lost. */
    daemon_stdout_stamp_flush_partial();
}


/* --- Startup helpers ----------------------------------------------------- */

static void daemon_print_usage(const char *argv0)
{
    printf("%s\n", LORAHAM_DAEMON_VERSION_TEXT);
    printf("\n");
    printf("Usage:\n");
    printf("  %s [options]\n", argv0);
    printf("\n");
    printf("Options:\n");
    printf("  -d, --daemon     run in the background, log: /tmp/lora_daemon.log\n");
    printf("  -v, --version    print the version and exit\n");
    printf("      --debug      enable the debug log\n");
    printf("      --radio MODE select the radio: 433, 868 (required)\n");
    printf("      --hw PRESET  hardware profile: %s\n",
           daemon_hardware_profile_known());
    printf("                   (default: loraham)\n");
    printf("      --tx-mode MODE      TX mode: direct, managed (default: managed)\n");
    printf("      --cad-monitor VAL   CAD=0/1 monitor: on, off (default: off)\n");
    printf("      --cad-rssi DBM      CAD busy threshold, integer dBm -130..0 (default: -90)\n");
    printf("      --high-power        permit POWER=20 on an SX127x board (+20 dBm; default: off)\n");
    printf("                          datasheet: duty cycle <= 1 %%, VSWR <= 3:1; nothing enforces it\n");
    printf("  -h, --help       print this help and exit\n");
    printf("\n");
    printf("Sockets (only those of the selected band are created):\n");
    printf("  DATA  433: %s\n", DATA433_SOCKET);
    printf("  DATA  868: %s\n", DATA868_SOCKET);
    printf("  DATAF 433: %s\n", DATA433_FRAMED_SOCKET);
    printf("  DATAF 868: %s\n", DATA868_FRAMED_SOCKET);
    printf("  CONF  433: %s\n", CONF433_SOCKET);
    printf("  CONF  868: %s\n", CONF868_SOCKET);
    printf("  (directory /run/loraham; group loraham for client access;\n");
    printf("   LORAHAM_SOCKET_DIR overrides it for dev and test runs only)\n");
    printf("\n");
}

static void daemon_print_version(void)
{
    printf("%s\n", LORAHAM_DAEMON_VERSION_TEXT);
}

static void daemon_print_startup_version(void)
{
    printf("[Daemon] %s\n", LORAHAM_DAEMON_VERSION_TEXT);
}

static bool daemon_parse_args(int argc, char *argv[])
{
    int opt;
    bool is_daemon = false;
    static const struct option long_options[] = {
        {"daemon",      no_argument, 0, 'd'},
        {"version",     no_argument, 0, 'v'},
        {"debug",       no_argument, 0, 1000},
        {"radio",       required_argument, 0, 1001},
        {"tx-mode",     required_argument, 0, 1002},
        {"cad-monitor",     required_argument, 0, 1005},
        {"cad-rssi",        required_argument, 0, 1008},
        {"hw",          required_argument, 0, 1011},
        {"rflog",       required_argument, 0, 1012},
        {"rflog-path",  required_argument, 0, 1013},
        {"high-power",  no_argument,       0, 1014},
        {"help",        no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    // Parse command-line options.
    while ((opt = getopt_long(argc, argv, "dvh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'd':
                is_daemon = true;
                daemon_debug_ctx("STARTUP", "option -d recognised");
                break;
            case 'v':
                daemon_print_version();
                exit(EXIT_SUCCESS);
            case 1000:
                daemon_log_level = DAEMON_LOG_DEBUG;
                daemon_debug_ctx("STARTUP", "debug active");
                break;
            case 1001:
                if (!daemon_parse_radio_selection(optarg)) {
                    fprintf(stderr, "invalid radio mode: %s\n", optarg ? optarg : "");
                    fprintf(stderr, "allowed: 433, 868\n");
                    daemon_print_usage(argv[0]);
                    exit(EXIT_FAILURE);
                }
                daemon_debug_ctx("STARTUP", "option --radio recognised: %s",
                                 daemon_radio_selection_name(daemon_radio_selection));
                break;
            case 1002:
                if (!daemon_set_tx_mode_boot_global(optarg)) {
                    fprintf(stderr, "invalid TX mode: %s\n", optarg ? optarg : "");
                    fprintf(stderr, "allowed: direct, managed\n");
                    daemon_print_usage(argv[0]);
                    exit(EXIT_FAILURE);
                }
                daemon_debug_ctx("STARTUP", "option --tx-mode recognised: %s", optarg);
                break;
            case 1005:
                if (!daemon_set_cad_monitor_boot_global(optarg)) {
                    fprintf(stderr, "invalid CAD monitor value: %s\n", optarg ? optarg : "");
                    fprintf(stderr, "allowed: on, off\n");
                    daemon_print_usage(argv[0]);
                    exit(EXIT_FAILURE);
                }
                daemon_debug_ctx("STARTUP", "option --cad-monitor recognised: %s", optarg);
                break;
            case 1008:
                if (!daemon_set_cad_rssi_boot_global(optarg)) {
                    fprintf(stderr, "invalid CAD RSSI value: %s\n", optarg ? optarg : "");
                    fprintf(stderr, "allowed: integer dBm between -130 and 0\n");
                    daemon_print_usage(argv[0]);
                    exit(EXIT_FAILURE);
                }
                daemon_debug_ctx("STARTUP", "option --cad-rssi recognised: %s", optarg);
                break;
            case 1011:
                if (!daemon_set_hardware_preset(optarg)) {
                    fprintf(stderr, "invalid hardware profile: %s\n", optarg ? optarg : "");
                    fprintf(stderr, "known: %s\n", daemon_hardware_profile_known());
                    daemon_print_usage(argv[0]);
                    exit(EXIT_FAILURE);
                }
                daemon_debug_ctx("STARTUP", "option --hw recognised: %s", optarg);
                break;
            case 1012:
                if (!daemon_set_rflog_switch_global(optarg)) {
                    fprintf(stderr, "invalid RF log switch (on|off): %s\n", optarg ? optarg : "");
                    exit(EXIT_FAILURE);
                }
                daemon_debug_ctx("STARTUP", "option --rflog recognised: %s", optarg);
                break;
            case 1013:
                if (!daemon_set_rflog_path_global(optarg)) {
                    fprintf(stderr, "invalid RF log path (an absolute path is required): %s\n", optarg ? optarg : "");
                    exit(EXIT_FAILURE);
                }
                daemon_debug_ctx("STARTUP", "option --rflog-path recognised: %s", optarg);
                break;
            case 1014:
                daemon_set_high_power_boot_global();
                daemon_debug_ctx("STARTUP", "option --high-power recognised");
                break;
            case 'h':
                daemon_print_usage(argv[0]);
                exit(EXIT_SUCCESS);
            default:
                daemon_print_usage(argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    // The RF log the controller asked for must exist before the radio does:
    // "on" without a path, or an unopenable path, is a startup error, never a
    // silently absent log.
    {
        char rflog_err[512];
        if (!daemon_rflog_start(rflog_err, sizeof(rflog_err))) {
            fprintf(stderr, "%s\n", rflog_err);
            exit(EXIT_FAILURE);
        }
    }

    if (optind < argc) {
        fprintf(stderr, "unknown argument: %s\n", argv[optind]);
        daemon_print_usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    if (!daemon_radio_selection_is_set()) {
        fprintf(stderr, "missing option: --radio (433 or 868)\n");
        daemon_print_usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    /* Selection is final: freeze the band descriptor, then resolve the
     * hardware preset against it; an unknown preset fails closed via the
     * standard usage-error path. */
    daemon_band_resolve(daemon_radio_433_enabled() ? RADIO_BAND_433
                                                   : RADIO_BAND_868);
    if (!daemon_hardware_profile_resolve(daemon_band()->band_number)) {
        fprintf(stderr, "invalid hardware profile: %s\n",
                daemon_hardware_preset_name());
        fprintf(stderr, "known: %s\n", daemon_hardware_profile_known());
        daemon_print_usage(argv[0]);
        exit(EXIT_FAILURE);
    }
    daemon_debug_ctx("STARTUP", "hardware profile: %s (%s)",
                     daemon_hw_profile.name,
                     daemon_chip_family_name(daemon_hw_profile.family));


    return is_daemon;
}

/* --- Boot high-power notice ---------------------------------------------- */
/*
 * Printed exactly once per process, after stdout is final (background mode
 * re-opens it) and before any client is served. Not in the driver: its boot
 * path re-runs on every MODE switch and would repeat the block. The full
 * contract is stated here because the log is where "the operator opted in"
 * is witnessed later; each later accepted SET POWER=20 adds one concise line
 * (sx127x_driver.cpp). On an SX1262 the flag changes nothing and says so.
 */
static void daemon_print_high_power_notice(void)
{
    if (!daemon_high_power_enabled())
        return;

    const char *tag = daemon_band()->tag;

    if (daemon_hw_profile.family != DAEMON_CHIP_FAMILY_SX127X) {
        printf("[%s] --high-power: not applicable on %s (0..20 dBm accepted "
               "unchanged)\n", tag, daemon_chip_family_name(daemon_hw_profile.family));
        return;
    }

    printf("[%s] WARNING SX127x +20 dBm permission enabled (--high-power); "
           "this flag does not select power\n", tag);
    printf("[%s] WARNING at +20 dBm: transmit duty cycle <= 1 %%, antenna "
           "VSWR <= 3:1, chip VDD 2.4-3.7 V (SX1276/7/8 datasheet 5.4.3)\n", tag);
    printf("[%s] WARNING provide proper cooling for the chip; cooling does not "
           "relax these limits\n", tag);
    printf("[%s] WARNING no duty-cycle measurement or enforcement exists; the "
           "operator is responsible\n", tag);
    printf("[%s] WARNING outside-spec operation can damage hardware; warranty "
           "void if disregarded\n", tag);
    fflush(stdout);
}

/* --- Boot TX mode application -------------------------------------------- */
static RadioTxMode_t daemon_boot_tx_mode_to_radio(DaemonTxModeBoot mode)
{
    return mode == DAEMON_TX_MODE_BOOT_DIRECT ? RADIO_TX_MODE_DIRECT
                                              : RADIO_TX_MODE_MANAGED;
}

// Startup-only: override the MANAGED default set by radio_controller_init with
// the CLI-resolved mode. Single-threaded; runs after daemon_io_init(). Only
// the selected band's controller is touched and logged.
static void daemon_apply_boot_tx_modes(void)
{
    RadioTxMode_t mode =
        daemon_boot_tx_mode_to_radio(daemon_tx_mode_boot_effective());

    radio_controller.tx_mode = mode;
    daemon_debug_ctx("STARTUP", "TX mode %s=%s",
                     daemon_band()->tag, radio_tx_mode_name(mode));
}

/* --- Boot CAD monitor application ---------------------------------------- */
// Startup-only: apply the CLI-resolved CAD monitor opt-in (default off). Lets
// legacy CONF clients get CAD=0/1 without issuing SET CADMONITOR. Runtime
// SET CADMONITOR can still override afterwards.
static void daemon_apply_boot_cad_monitor(void)
{
    bool mon = daemon_cad_monitor_boot_effective();
    float rssi = 0.0f;
    bool rssi_set = daemon_cad_rssi_boot_effective(&rssi);

    radio_controller.cad_monitor_active.store(mon);
    daemon_debug_ctx("STARTUP", "CAD monitor %s=%d",
                     daemon_band()->tag, mon ? 1 : 0);
    // CAD RSSI threshold override (unset keeps the default).
    if (rssi_set) {
        radio_controller.cad_rssi_threshold_dbm.store(rssi);
        daemon_debug_ctx("STARTUP", "CAD-RSSI %s=%.0f",
                         daemon_band()->tag, (double)rssi);
    }
}

/* --- Startup sequence ---------------------------------------------------- */
static void daemon_startup_sequence(int argc, char *argv[])
{
    daemon_lifecycle_ignore_sigpipe();
    daemon_debug_ctx("STARTUP", "SIGPIPE is ignored");
    bool is_daemon = daemon_parse_args(argc, argv);

    daemon_debug_ctx("STARTUP", "start mode: %s", is_daemon ? "daemon" : "foreground");
    daemon_debug_ctx("STARTUP", "radio selection: %s",
                     daemon_radio_selection_name(daemon_radio_selection));
    daemon_debug_ctx("STARTUP", "arguments parsed");

    // Enter background mode when requested.
    if (is_daemon) {
        daemon_lifecycle_enter_background();
        daemon_debug_ctx("STARTUP", "daemon mode active");
    }

    /* After any redirection of stdout, and before the first line the operator
     * will read: background mode re-opens the fd, so installing earlier would
     * stamp into a stream that is about to be replaced. */
    daemon_stdout_stamp_install();

    daemon_print_startup_version();
    daemon_print_high_power_notice();

    daemon_debug_ctx("STARTUP", "starting radio and socket init");
    daemon_io_init();
    daemon_apply_boot_tx_modes();
    daemon_apply_boot_cad_monitor();
    daemon_debug_ctx("STARTUP", "startup complete");
}

/* --- Main entry ---------------------------------------------------------- */
int main(int argc, char *argv[])
{
    /* Line-buffered stdout even when redirected to a file (nohup, systemd,
     * pipes): the RX/TX log lines must appear as they happen, not when a
     * full stdio buffer spills — operators tail these logs live. */
    setvbuf(stdout, NULL, _IOLBF, 0);

    daemon_startup_sequence(argc, argv);
    daemon_run();

    return 0;
}
