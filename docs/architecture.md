# Architecture

How `loraham_daemon` is put together: what each source module does, how the process starts,
runs and stops, and how the radio, the sockets and the TX worker share one piece of hardware.
Board wiring and `--hw` presets are in [Hardware](hardware.md), the flags themselves in
[CLI](cli.md), and the wire formats in [CONF protocol](conf-protocol.md) and
[DATA protocol](data-protocol.md).

## Contents

- [Purpose and shape](#purpose-and-shape)
- [Process and lifecycle](#process-and-lifecycle)
- [Startup and shutdown](#startup-and-shutdown)
- [Radio hardware and runtime](#radio-hardware-and-runtime)
- [I/O, sockets and clients](#io-sockets-and-clients)
- [DATA, RX and TX runtime](#data-rx-and-tx-runtime)
- [CONFIG and monitoring](#config-and-monitoring)
- [Concurrency and lock ordering](#concurrency-and-lock-ordering)

## Purpose and shape

The daemon controls the LoRaHAM radio hardware from user space as a single-radio daemon: one
process per band. It exists to

- provide raw DATA sockets for backward-compatible stream TX/RX,
- provide framed DATA sockets that preserve packet boundaries for clients,
- provide CONF sockets for runtime radio configuration,
- keep radio access centralized, so client programs need no direct SPI/RadioLib access,
- print live RX/TX/debug information when run in the foreground.

For dual-band operation the daemon runs as two independent processes, one per band, each
startable, stoppable and restartable on its own; see [Deployment](deployment.md).

In background mode (`-d`) the same output is redirected to `/tmp/lora_daemon.log`.

Two behaviours follow from the socket design and are worth stating once. The raw and the framed
DATA socket of the same band share the same radio backend. Multiple clients may connect to the
same band, but TX arbitration is best-effort and event-loop ordered — clients that need exclusive
radio use must coordinate externally. Framed DATA is packet transport only; higher-level
protocols stay in the clients.

## Process and lifecycle

| Area | File/module | Role |
|---|---|---|
| Main daemon / orchestration | `loraham_daemon.cpp` | Process entry point, CLI/startup, runtime context, main loop, and high-level coordination of I/O, socket dispatch, radio polling, monitoring, and shutdown |
| Public daemon constants | `daemon_protocol.h` | Public socket paths plus the buffer size `buf_SIZE`, the per-channel client limit `MAX_CLIENTS`, and the CAD cadence constant `DAEMON_CAD_POLL_INTERVAL_MS` (values in [Limits](limits.md)) |
| Version | `daemon_version.h` | Single source for the daemon version printed by `--version` and at startup |
| Logging | `daemon_log.cpp`, `daemon_log.h` | Normal and debug logging helpers used by daemon/runtime modules |
| Timing | `daemon_timing.cpp`, `daemon_timing.h` | Monotonic time (`daemon_now_ms`), counter ticks, the RSSI stream cadence, and the generic `DaemonDeadlineTimer` used for the CAD, RSSI and stats timers. The stats interval itself is not here: `DAEMON_STATS_LOG_INTERVAL_MS` (`3600000` ms) lives in `daemon_stats.h` |
| Runtime statistics | `daemon_stats.cpp`, `daemon_stats.h` | The `DaemonRadioStats` block (RX packets/bytes/drops, TX ok/errors/busy, CAD timeouts, CAD timeout sends, RX re-arm failures), uptime, the periodic stats interval, and the field formatting shared by the operator log line and the CONF reply |
| Lifecycle | `daemon_lifecycle.cpp`, `daemon_lifecycle.h` | SIGPIPE handling, daemon background mode, inherited-fd cleanup, stop flag handling, and signal-based shutdown |
| Boot TX mode | `daemon_tx_mode_boot.cpp`, `daemon_tx_mode_boot.h` | The `--tx-mode` boot slot: parse, store, and resolve unset to `MANAGED` |
| Boot CAD monitor | `daemon_cad_monitor_boot.cpp`, `daemon_cad_monitor_boot.h` | The `--cad-monitor` boot slot: parse, store, and resolve unset to off; runtime `SET CADMONITOR` still overrides |
| Boot CAD RSSI | `daemon_cad_rssi_boot.cpp`, `daemon_cad_rssi_boot.h` | The `--cad-rssi` boot slot: parse a dBm threshold, store it, and leave the controller default in place when unset |
| Boot high-power permission | `daemon_high_power_boot.cpp`, `daemon_high_power_boot.h` | The `--high-power` boot flag: a bare, immutable per-process permission for `POWER=20` on SX127x; supplied to the CONFIG prevalidation at the `daemon_config_runtime.cpp` boundary, to the SX127x driver at creation, and reported in `STATUS` |

The main loop is a single thread. Each iteration waits for socket events, processes the ready
sockets, then polls the radio (RX drain plus the CAD, RSSI and stats monitoring ticks), then
reconciles the event-loop watch set. It repeats until the lifecycle stop flag is set.

## Startup and shutdown

Startup runs in this order:

1. SIGPIPE is ignored, then the command line is parsed. The band descriptor is resolved once at
   the end of argument parsing.
2. Background mode is entered if `-d` was given, and the version banner is printed.
3. `daemon_io_init()` takes the per-band instance-ownership lock first, then installs the
   stop-signal handlers — before sockets, GPIO, SPI and radio setup, so a `SIGTERM` during the
   rest of startup still exits cleanly.
4. Client slots and channel I/O are initialised, the GPIO pin locks are acquired and the LED is
   claimed, the socket files are opened, and the radio controller and RadioLib are brought up.
5. The boot `--tx-mode` and `--cad-monitor` settings are applied, then the event backend is
   started and the polling loop begins.

Four conditions are fatal at startup:

| Condition | Result |
|---|---|
| Signal-handler setup fails | Releases the instance lock, exits `EXIT_FAILURE` |
| Socket setup for the selected radio fails | Runs the I/O startup cleanup, exits `EXIT_FAILURE` |
| The selected radio does not become ready | Cleans up, then exits `4` (`LORAHAM_EXIT_LOCK_ERROR`) on a boot lock-infrastructure failure, otherwise `EXIT_FAILURE` |
| Event-loop setup fails | Runs the shutdown cleanup, exits `EXIT_FAILURE` |

Every log line, on stdout **and** stderr, begins with a UTC timestamp in the same format the RF log
uses — `2026-09-16T08:22:31.003Z `. A line is often built from several `printf` calls (the CONFIG
apply prints one coloured fragment per key; the pin-lock line prints one call per pin), so the
stamping is done by a stream wrapper that accumulates until the newline rather than per call, with
a per-thread buffer so concurrent writers cannot splice their half-lines together. It is installed
after any redirection, so background mode is stamped too.

Normal logs report the active radio once during startup, as `[Daemon] active radios: <tag>`
(or `none`). Debug logs (`--debug`) additionally carry the selected-radio decisions
(`option --radio recognised: …`, `radio selection: …`).

On shutdown, only the selected radio's sockets and client slots are cleaned up — there is one
band per process, frozen at startup. In the async TX worker, queued jobs that have not started
are discarded and new queued submissions are rejected, while a job that was already dequeued is
allowed to finish. The instance lock is released last, after all sockets are closed and unlinked.

## Radio hardware and runtime

| Area | File/module | Role |
|---|---|---|
| Radio selection | `daemon_radio_selection.cpp`, `daemon_radio_selection.h` | Parses and exposes the selected band, `433` or `868`; unset until the mandatory `--radio` is given |
| Band descriptor | `daemon_band.cpp`, `daemon_band.h` | The one immutable per-process band context, resolved once from `--radio`: band/tag, socket paths, log contexts, the operational frequency policy `freq_min_mhz`/`freq_max_mhz` that gates `SET FREQ`, RF boot defaults, legacy LED pin. Runtime code reads its band from here — no per-band globals, no duplicated 433/868 paths. The socket paths are the frozen defaults unless `LORAHAM_SOCKET_DIR` (dev/test only) is set, in which case `daemon_band_resolve` rewrites the three paths into static buffers at resolve time |
| Hardware profiles | `hardware_profile.cpp`, `hardware_profile.h` | The `--hw` preset table — wiring, chip family, capabilities, LED, claimed pins; see [Hardware](hardware.md) |
| RF boot defaults | `radio_rf_defaults.h` | The `RadioRfDefaults` struct applied inside `RadioDriver::begin()`, including the LDRO convention (`<0` = `autoLDRO()` only, `>=0` = `autoLDRO()` then `forceLDRO(value)`) |
| Radio driver interface | `radio_driver.h` | Runtime interface between the chip-agnostic daemon and one chip family: generic `PhysicalLayer` delegation plus pure-virtual chip specifics — `begin()` with RF defaults/TCXO, mode switch, CONF parameter apply (`applyLoraParam`/`applyFskParam`), and raw RSSI (`readLiveRssi`/`rssiProbe`) |
| SX127x driver | `sx127x_driver.cpp`, `sx127x_driver.h` | Concrete driver for SX1278/RFM9x — all SX127x register constants live here, including the D8 `begin()`-failure diagnosis |
| SX1262 driver | `sx1262_driver.cpp`, `sx1262_driver.h` | Concrete driver for the SX126x family: TCXO via DIO3, DIO2-as-RF-switch plus inverse antenna-switch line, SX126x CRC/sync/power semantics, `GetRssiInst` live RSSI |
| Shared driver output | `driver_config_print.h` | The per-key CONFIG apply state output and the FSK `SHAPING` parser, shared by all `RadioDriver` implementations so the output format stays byte-identical |
| Radio controller state | `radio_controller.h` | HAL/Module/driver ownership, radio health and mode flags, RX callback state, the RX re-arm state machine (`rx_rearm_pending`, `rx_rearm_consecutive_failures`, `rx_rearm_next_retry_ms`), TX/CAD/RSSI flags, the default CAD busy threshold `RADIO_CAD_RSSI_BUSY_THRESHOLD_DBM`, the `cad_scan_available` hardware capability flag, the embedded `DaemonRadioStats` block, the LED pin — and the lock-ordering contract |
| Radio runtime | `daemon_radio_runtime.cpp`, `daemon_radio_runtime.h` | The single `radio_controller` instance: setup and shutdown from the band descriptor, RX callback glue (`setFlag`), LED sync, readiness, and active-radio logging |
| Radio startup/init | `daemon_radio_init.cpp`, `daemon_radio_init.h` | Profile-driven `Module` construction, driver selection by chip family, RF boot defaults from the band descriptor, callback install, initial `startReceive()`, and the family-aware `begin()`-failure diagnosis dispatch |
| RX re-arm | `daemon_rx_rearm.cpp`, `daemon_rx_rearm.h` | Captures every `startReceive()` result so a failed re-arm can never leave a `READY`-but-deaf radio: one latched log line per incident, the `rx_rearm_failures` counter, a backoff retry in the main loop's radio tick, escalation to `FAILED` after `DAEMON_RX_REARM_FAIL_LIMIT` consecutive failures, and a fail-closed boot path |
| SPI transaction lock | `locking_pihal.h` | `LockingPiHal` — the RadioLib `PiHal` subclass that serializes each SPI transaction across processes and bands via a shared `flock`; fails closed if the lock cannot be established |
| GPIO pin locks | `daemon_gpio_lock.cpp`, `daemon_gpio_lock.h` | One advisory `flock` per pin in the trusted runtime lock directory, taken non-blocking in ascending pin order before any lgpio claim. Needed because lgpio's claim API prints and swallows errors, so RadioLib cannot report a pin conflict and a conflict would otherwise be silent. A held pin fails the boot closed |
| Instance ownership lock | `daemon_instance_lock.cpp`, `daemon_instance_lock.h` | Per-band lifetime `flock` ownership lock, acquired before sockets and released after socket cleanup; rejects same-band duplicates and prevents the shutdown/restart socket race |
| Shared runtime/lock paths | `loraham_runtime.h` | Trusted lock-directory resolution and validation (`/run/lock/loraham`, `O_DIRECTORY`/`O_NOFOLLOW`, owned by root or the daemon user and not group/world-writable, `openat` lock files; `LORAHAM_RUNTIME_DIR` dev override), stable exit codes, and the EINTR-only `flock` acquire/release helpers |
| Radio health | `radio_health.cpp`, `radio_health.h` | The `UNINITIALIZED`/`READY`/`FAILED` state and the readiness helpers that guard CONFIG and TX behaviour |
| LED/GPIO helpers | `daemon_led.cpp`, `daemon_led.h` | Raspberry Pi GPIO LED setup and per-radio LED pin state control. The LED is a per-band hardware/activity resource, not the instance-ownership lock — that is `daemon_instance_lock` |

## I/O, sockets and clients

| Area | File/module | Role |
|---|---|---|
| Daemon I/O runtime | `daemon_io_runtime.cpp`, `daemon_io_runtime.h` | Owns the band's socket fds, client slots, framed client state and channel state, plus I/O startup/cleanup and the persistent event-watch reconciliation |
| Radio channel I/O | `radio_channel.cpp`, `radio_channel.h` | Per-band raw DATA, framed DATA and CONF socket descriptors, socket setup/open helpers, and the client accept/flush flow. Live-RSSI reads are not here: the raw register access is chip-specific and moved into `RadioDriver::readLiveRssi` |
| Event loop | `event_loop.cpp`, `event_loop_epoll.cpp` | A thin wrapper over the epoll backend — `EVENT_LOOP_BACKEND_EPOLL` is the only backend defined — plus epoch-based reconciliation of the persistent epoll watches for socket readiness |
| UNIX sockets | `unix_socket.cpp`, `unix_socket.h` | Create, bind, listen, close and remove local UNIX socket files; stale socket paths are replaced, non-socket path collisions are rejected |
| Socket runtime | `daemon_socket_runtime.cpp`, `daemon_socket_runtime.h` | Logged per-channel accept/flush helpers around the socket client slots |
| Socket dispatch | `daemon_socket_dispatch.cpp`, `daemon_socket_dispatch.h` | Ready-socket orchestration in a fixed order — accept, raw DATA, framed DATA, completion drain, CONFIG, flush — with log tags taken from the band descriptor |
| Client handling | `client_output_queue.cpp`, `client_slot.cpp` | Unified client slots (fd plus output queue, stream buffer and generation), nonblocking I/O, queued output, disconnect cleanup and broadcast helpers |

When a client disconnects the daemon closes the slot and can reuse it. The slot generation is
what lets a late TX completion be matched to the client that asked for it, rather than to whoever
inherited the slot.

## DATA, RX and TX runtime

| Area | File/module | Role |
|---|---|---|
| Raw DATA TX | `data_tx.cpp`, `data_tx.h` | Reads the raw DATA sockets and splits client writes into RF-sized chunks at `DATA_TX_MAX_CHUNK_SIZE` |
| Shared DATA TX runtime | `daemon_data_tx_runtime.cpp`, `daemon_data_tx_runtime.h` | Applies the radio-health checks, TX-busy policy, CAD policy, TX queue selection, TX result state, stats and DATA TX logging |
| TX policy | `daemon_tx_policy.h` | Central TX-busy timeout, CAD wait timeout, stable-idle window, poll interval and the send-after-CAD-timeout policy; see [Limits](limits.md) |
| TX executor / job / outcome | `daemon_tx_executor.cpp`, `daemon_tx_executor.h`, `daemon_tx_job.h`, `daemon_tx_outcome.h` | The internal TX job and result structures, the TX result mapping, and the RadioLib send seam; the executor bodies live in `daemon_tx_executor.cpp` |
| TX queue / worker | `daemon_tx_queue.*`, `daemon_tx_worker.*`, `daemon_tx_async_worker.*`, `daemon_tx_async_runtime.*` | The bounded reject-newest TX queue, the process's one async TX worker and its lifecycle, the accepted/rejected/processed counters, and the single completion queue |
| TX completion bridge | `daemon_tx_completion.cpp`, `daemon_tx_completion.h` | Encodes final async TX results as framed `TX_RESULT`, targets the originating framed slot, and drops stale completions on a client-slot generation mismatch (`DAEMON_TX_COMPLETION_DELIVERY_STALE`) |
| Radio TX path | `daemon_tx.cpp`, `daemon_tx.h` | Validates TX requests against the band and radio readiness, validates the RF packet, prepares and restores the radio TX state (IRQ clear, callback re-install, `startReceive()`), and maps RadioLib results to `TX_RESULT_OK` / `RADIO_ERROR`. The `TX=1/0` CONF broadcast itself is driven from the monitoring tick off the TX status generation |
| Framed DATA protocol | `framed_data.cpp`, `framed_data_tx.cpp` | Binary frame helpers, framed TX stream state, `ERROR` frames, `TX_RESULT` frames and `RX_PACKET` framing; see [DATA protocol](data-protocol.md) |
| Framed DATA runtime | `daemon_framed_data_runtime.cpp`, `daemon_framed_data_runtime.h` | The framed DATA socket read loop, `TX_PACKET` forwarding, immediate and final `TX_RESULT` behaviour, `ERROR` handling and async completion draining |
| RX runtime | `daemon_rx.cpp`, `daemon_rx.h` | The RX packet read/validate/print/forward flow for raw and framed clients; it returns without blocking while a TX is in flight, checking `tx_busy` before and after taking the lock. IRQ order contract: the radio's IRQ flags are **not** cleared before `readData()` — RadioLib derives the CRC / header verdict from them at read time (`RADIOLIB_ERR_CRC_MISMATCH`, `RADIOLIB_ERR_LORA_HEADER_DAMAGED` → the frame is dropped and counted in `rx_drops`) and clears them itself afterwards; the daemon clears again only before re-arming |
| RF packet / TX result | `rf_packet.cpp`, `tx_result.cpp` | RF payload validation and preview helpers, and the normalized TX result states |

After a mode reinitialization the packet-received callback is restored and RX is restarted.

## CONFIG and monitoring

| Area | File/module | Role |
|---|---|---|
| CONFIG stream/parser/apply | `config_stream.cpp`, `config_parser.cpp`, `config_value.cpp`, `config_policy.cpp`, `config_validate.cpp`, `config_apply.cpp`, `config_status.*`, `config_dispatch.*` | Line framing, strict parsing, validation policy, whole-command prevalidation followed by sequential apply, and dispatch support for `SET KEY=VALUE`. The chip-specific per-key application lives in the radio drivers, not here — see `applyLoraParam`/`applyFskParam` in `radio_driver.h` |
| CONFIG runtime | `daemon_config_runtime.cpp`, `daemon_config_runtime.h` | Builds the CONFIG dispatch context from the band descriptor and supplies the debug logging callbacks |
| Monitoring runtime | `daemon_monitoring.cpp`, `daemon_monitoring.h` | CAD status broadcast and its de-flicker policy, `TX=1/0` status broadcast, live RSSI streaming, `GETRSSI` auto-stop and the periodic operator stats line |
| CAD probe core | `radio_cad.cpp`, `radio_cad.h`, `daemon_cad_monitor.h` | The passive RSSI probe (try-lock, TX-gated), the active `scanChannel` probes used for TX gating and `GET CHANNEL`, and the CAD monitor tick |

The command set these modules implement is documented in [CONF protocol](conf-protocol.md).

## Concurrency and lock ordering

The daemon has one main thread and one async TX worker, and takes three kinds of lock.

`LockingPiHal` (`locking_pihal.h`) subclasses RadioLib's `PiHal` and takes a process-shared
advisory `flock` around each complete SPI transaction: `spiBeginTransaction()` takes `LOCK_EX`,
`spiEndTransaction()` releases with `LOCK_UN`. That serializes the full CS-low → transfer →
CS-high window across processes while leaving the bus free between transactions. The lock is
held only for one short transfer, never for the daemon lifetime, and the kernel releases it
automatically on process death — so a crashed peer's SPI lock is released immediately.

The ordering rule is: the in-process `radio_mutex` is always taken before the low-level SPI
`flock`, never the reverse. There is no inverse path, so the pair cannot deadlock.

The access rule follows from the TX worker being allowed to hold `radio_mutex` across a blocking
`transmit()` — seconds at SF12. No main-loop path may block on `radio_mutex` while a TX can be in
flight. Monitoring, RX and status ticks therefore gate on `tx_busy` and acquire with
`try_to_lock`, and **skip** their sample rather than stalling. The one deliberate exception is
CONFIG apply, which blocks: it is client-initiated, rare, and must not be silently dropped.

The GPIO pin locks are a separate, cross-process mechanism, described under
[Radio hardware and runtime](#radio-hardware-and-runtime). The per-band instance-ownership lock
and the lock directory on disk are covered in [Deployment](deployment.md).
