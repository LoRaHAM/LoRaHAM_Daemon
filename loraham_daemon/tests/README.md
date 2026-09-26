# LoRaHAM daemon tests

This directory contains the daemon test sources.

## Usage

Run the regular test suite from the daemon source directory:

```bash
./run_tests.sh
```

Or from this `tests/` directory:

```bash
../run_tests.sh
```

The runner builds the production daemon with `build.sh`, builds the test
binaries, and then runs each test against the built daemon binary.

RF transmit tests are disabled by default. Enable them only with suitable
hardware, frequency settings, and RF conditions:

```bash
./run_tests.sh --TX --rx-seconds 15
```

## Runner behavior

`run_tests.sh` runs every binary in its `test_binaries` list. It refuses to start if a
`loraham_daemon` process is already running, checks for lingering daemon
processes after each test, parses per-test `Summary:` lines, and prints a final
OK/FAIL/SKIP/XFAIL/XPASS table.

### Radio hardware

A daemon instance only stays up once its radio reports ready, and no radio can
be probed without an SPI device. `test_interface_baseline`, `test_config_stream`,
`test_client_lifecycle`, `test_conf_status`, `test_conf_stats` and
`test_rssi_multiclient` therefore check for `/dev/spidev0.*` before they start a
daemon and report `SKIP` for that part where there is none — a machine without
radio hardware, which includes every hosted CI runner. Everything those binaries
do without a daemon still runs there: `test_interface_baseline` keeps its CLI
cases, the waveshare fail-closed check and the held-GPIO-lock exit. It skips
three further cases individually, because they too need a radio: the two
single-radio socket modes, and the unusable-`spi0.lock` exit, which is only
reached once the band gets as far as taking the SPI lock.

One case runs on the opposite condition: `unavailable gpiochip0 exits
LOCK_ERROR` needs a host WITHOUT `/dev/gpiochip0` and skips where the node
exists, because there the daemon gets past LED setup and the case would pass
without proving anything.

The check is for the precondition, not for a daemon that failed: where an SPI
device exists these tests run for real and a daemon that does not come up is a
`FAIL`, so the skip cannot mask a regression. `test_multi_instance` uses the
same rule.

## Test concept

The tests should protect externally relevant behavior:

- daemon build, startup, shutdown, and signal handling
- public command-line behavior, including selected-radio mode
- public UNIX socket behavior and cleanup
- CONFIG command parsing, validation, rejection, and apply safety
- raw DATA socket chunking, framed DATA helpers, and RF packet handling helpers
- client lifecycle, queued output, and slow/nonblocking client behavior
- timing, event-loop, radio-health, and TX-result semantics that affect daemon behavior


## Coverage overview

CONFIG/protocol:

- `test_config_parser`
- `test_config_stream_buffer`
- `test_config_stream`
- `test_config_value`
- `test_config_policy`
- `test_config_validate`
- `test_config_apply_transactional`
- `test_config_dispatch`

DATA/RF/TX:

- `test_data_tx`
- `test_data_tx_queue_runtime`
- `test_tx_result`
- `test_daemon_tx_outcome`
- `test_daemon_tx_policy`
- `test_daemon_tx_job`
- `test_daemon_tx_executor`
- `test_daemon_tx_completion`
- `test_daemon_tx_queue`
- `test_daemon_tx_worker`
- `test_daemon_tx_async_worker`
- `test_daemon_tx_async_runtime`
- `test_radio_tx_limit` (the payload rule: SX127x+FSK is 63 -- the 64-byte FIFO minus the length
  byte that variable-length mode puts in it -- and nothing else narrows; the live wrapper answers
  with the maximum when there is no radio to ask; and the three 255 storage ceilings stay 255,
  because narrowing them would cripple LoRa and turn a radio constraint into a wire-protocol one)
- `test_rf_packet`
- `test_framed_data` (including `TX_RESULT` layout)
- `test_framed_data_tx`
- `test_tx_failure_keeps_client`

Client/socket/runtime:

- `test_client_output_queue`
- `test_client_nonblocking`
- `test_client_queued_broadcast`
- `test_client_slow_output`
- `test_event_loop_output_flush`
- `test_client_lifecycle`
- `test_rssi_multiclient`
- `test_unix_socket`

Lifecycle/helper behavior:

- `test_daemon_radio_selection`
- `test_hardware_profile` (`--hw` preset resolution: legacy per-band identity, Uputronics NC pins/capabilities/slot LEDs, Waveshare SX1262 pin set, unknown-preset rejection)
- `test_tx_mode_boot` (`--tx-mode` parsing, default MANAGED, fail-closed on invalid values)
- `test_cad_monitor_boot` (`--cad-monitor` parsing, default off, fail-closed on invalid values)
- `test_high_power_boot` (`--high-power`: unset is OFF, the bare flag is ON, idempotent, reset)
- `test_event_loop` (persistent reconciliation, mask changes, stale removal, and fd reuse)
- `test_daemon_timing`
- `test_daemon_lifecycle`
- `test_radio_health`
- `test_sx127x_power_register` (real driver + pinned RadioLib on the register model: `POWER=20` writes `RegPaDac` 0x87 with OCP 140 mA in one apply, 17 / a `MODE` switch / a boot over a boosted chip restore 0x84 + 100 mA, no permission or 18/19 touch nothing, a rejected write is a returned error)
- `test_radio_cad_probe` also pins the duplicate-RX regression the live matrix caught: the probe
  takes the packet-received alert OFF DIO0 before scanning and reinstalls it after, because
  `startChannelScan()` remaps that pin from RxDone to CadDone -- leaving it attached made CAD
  completion fire the RX callback and re-deliver the previous packet from a stale FIFO.
- `test_radio_cad_probe` (incl. capability gating: a profile that declares no trustworthy active CAD
  never calls scanChannel and answers from the passive RSSI probe; and the HW-3 preconditions --
  outside LoRa or while an RX re-arm is pending the passive probe makes ZERO radio calls and the
  active probes keep their non-destructive snapshot but run neither the CAD nor the re-arm)
- `test_sx127x_cad_register` (register-polled CAD on the REAL `Sx127xDriver` against the REAL pinned
  RadioLib, with `tests/fakes/sx127x_register_model.h` where the chip would be: CadDone alone is
  FREE, CadDone+CadDetected in the same sample is BUSY, a chip that never asserts CadDone is a
  bounded error and not a verdict, the flags are tested before they are cleared and none are left
  standing, the chip is returned to standby, no pin is consulted on either the DIO1-routed or the
  DIO1-less profile, and the deadline is computed from the driver's cached SF/BW -- seeded at boot,
  moved by a successful `SET`, NOT moved by one the chip rejected, and reset by LoRa re-entry.
  It also pins POWER+OCP as one setting: RegOcp reads 100 mA -- the silicon default -- after boot,
  after `SET POWER` and after a switch in BOTH directions, because RadioLib re-pins it to 60 mA
  inside `beginFSK()`, below the 87 mA the PA draws at +17 dBm. And `SET POWER` 0/18/19/20 write no
  register at all. And LDRO:
  boot writes the bit the configuration needs, `SET LDRO=AUTO` CLEARS a stale bit left in the chip
  by a previous run (RadioLib's `autoLDRO()` sets a flag and writes nothing), AUTO keeps tracking
  later SF/BW changes, and an explicit `LDRO=0` still wins where AUTO would have set it. Finally TX:
  a transmission whose DIO0 never asserts returns `ERR_TX_TIMEOUT`, never `ERR_NONE` -- which is
  what makes the HAL's "answer LOW on a read error" safe, since the wait is bounded at 150 % of
  the computed time-on-air -- and the chip is still parked in standby afterwards)
- `test_cad_monitor_state` (opt-in `CAD=0/1` CONF monitor: single-edge emission, RX-pending must not suppress `CAD=0`, free-confirmation hysteresis/dead band, non-destructive to RX, and latch-reset semantics)

Multi-instance (split per-band) operation:

- `test_daemon_led` (selection-aware LED ownership: 433-only / 868-only claims, duplicate-band rejection is fatal, and profile-disabled LED (`led_pin` NC) stays healthy without claims)
- `test_instance_lock` (per-band instance-ownership locks: 433/868 ownership, duplicate rejection, release-unblocks-restart, and shared-lock inode stability)
- `test_locking_pihal` (process-shared SPI transaction lock: cross-process exclusion, recursion guard, fail-closed when the lock dir is unusable, no transfer without the lock, EINTR-retry vs hard-failure on both lock and unlock, and fatal-on-hard-unlock; no radio hardware needed)
- `test_hal_gpio_failclosed` (HAL GPIO failure semantics: an lgpio read error is never surfaced as a pin
  level and never as a truthy one -- RadioLib's TX and CAD waits are `while(!digitalRead(irq))`, so a
  negative code read through `uint32_t` would end the wait at once and report success; a failed
  gpiochip open does not proceed to SPI and does not block a retry; and a read on a pin whose alert was
  detached stays legal, because every TX detaches and reinstalls it. Injects lgpio failures through
  `tests/fakes/lgpio.h`, so no library and no hardware are involved)
- `test_runtime_lockdir` (trusted lock-directory/file validation: missing, symlink, non-directory, group/world-writable, non-root-owner-when-required, regular-file and non-regular/symlink lock files, and override-mode directory creation)
- `test_stdout_stamp` (UTC timestamps on the log: a line built from SIX printf calls comes out as
  ONE stamped line -- which is why this is a stream wrapper and not a stamped printf -- stderr is
  stamped too so a redirected log is not half-timed, two concurrent threads never splice their
  half-lines together, and an unterminated line is still flushed at shutdown. Each case runs in a
  forked child, because the wrapper replaces `stdout` for the whole process)
- `test_packaging` (deployment artifacts: `systemd/tmpfiles.d/loraham.conf` exists and documents `/run/lock/loraham`; the unit has no `RuntimeDirectory`/`EnvironmentFile` and keeps `RestartPreventExitStatus`)
- `test_multi_instance` (integration: duplicate same-band rejection with socket survival, simultaneous 433+868, and independent shutdown; requires radio hardware)

Public integration baseline:

- `test_interface_baseline` (CLI incl. `--hw` preset acceptance/rejection, per-band socket exposure, waveshare-profile fail-closed without HAT, LoRa/FSK config, RF write paths, and the startup exit codes: a held GPIO lock, an unusable `spi0.lock` and an unopenable `gpiochip0` all exit `LORAHAM_EXIT_LOCK_ERROR` (4, restart-suppressed), never the restartable 1)


## CAD/TX rework guardrail

`test_tx_failure_keeps_client` verifies the M1 behavior: recoverable RF/TX execution failures are reported through the existing ERROR path without closing the framed client connection.

- `test_conf_status` covers `TXRESULT`, `TXMODE`, and `GET CHANNEL` CONF state reporting.
- `test_radio_cad_probe` verifies the real-CAD helper and the DIRECT-mode immediate (no-CAD) TX behavior.

- `test_daemon_tx_outcome` verifies internal TX outcome to framed TX_RESULT status mapping.

- `test_daemon_tx_job` verifies future TX job/result data structures without changing TX behavior.

- `test_daemon_tx_executor` verifies the synchronous TX executor seam and raw `TxResult` preservation without changing daemon TX behavior.

- `test_daemon_tx_queue` verifies the bounded TX queue contract and synchronous drain seam without changing daemon TX behavior.

- `test_daemon_tx_worker` verifies the synchronous TX worker test facade and drain seam.

- `test_data_tx_queue_runtime` also pins two listen-before-talk invariants: a hardware CAD error is
  never converted into a transmission by `CADTXAFTERTIMEOUT` (which is an opt-in over valid BUSY
  observations, not over broken scans), while a genuinely busy channel still follows the opt-in;
  and the synchronous and queued CAD wait loops reach the SAME decision from the same scripted
  FREE/BUSY/ERROR sequence, so the `TXQUEUE` setting does not quietly change LBT behaviour.
- `test_data_tx_queue_runtime` also pins the FSK payload boundary at the consumer that matters:
  62 and 63 bytes are sent, 64 and 255 are rejected as INVALID_PACKET before the sender is called
  and before anything is queued, and LoRa still carries the full 255.
- `test_data_tx_queue_runtime` verifies the opt-in DATA TX async queue path, last-completion bookkeeping, target/sequence/generation propagation, completion queue handoff, RAW/MANAGED CAD wait policy behavior, MANAGED stable-idle enforcement, CAD-timeout flag preservation, and synchronous TX-busy timeout behavior with fast bounded test limits while keeping default DATA TX direct.

- `test_daemon_tx_async_worker` verifies the standalone async TX worker lifecycle.

- `test_daemon_tx_async_worker_stress` verifies concurrent submit/stop accounting.

- `test_daemon_tx_async_runtime` verifies daemon-owned async TX worker lifecycle state without live TX routing.

- `test_daemon_tx_completion` verifies encoding internal TX completion results as framed `TX_RESULT` frames, bounded completion queue behavior, targeted slot delivery, stale slot-generation rejection and accounting, main-loop drain delivery, and final-only queued result policy.

- `test_daemon_tx_policy` verifies the central CAD/TX timing policy constants and pure helper behavior used by the DATA TX CAD wait path.

- `test_daemon_stats_cad_timeout_send` verifies the dedicated MANAGED send-after-CAD-timeout statistics field.
