# Changelog

## 1.2.0

- `--high-power`: the explicit opt-in for `POWER=20` on an SX127x board. A bare, per-process boot
  flag — present means on, absent means off — that is the only thing which lets the whole-command
  prevalidation admit exactly `20`; no CONF command can grant it, so a client that pushes `POWER=20`
  on connect is refused the same way it was in 1.0.0, now with the reason
  `high-power mode not enabled (start with --high-power)` in the log. 18 and 19 stay refused with or
  without it (the pinned RadioLib PA_BOOST API admits 2–17 and exactly 20). On an SX1262 the flag is
  accepted and changes nothing.
- The +20 dBm setting is paired with the over-current limit it needs: 140 mA (OcpTrim 17) is written
  in the same apply as the boosted `RegPaDac` (0x87), and 100 mA comes back on every path that leaves
  20 — a lower `SET POWER`, a `MODE` switch that reloads the boot defaults, and a boot over a chip a
  previous process left boosted. 140 mA is a project choice above the datasheet's 120 mA typical
  draw at +20, not a Semtech-prescribed figure, and it has not been current-measured on these boards.
- What the flag does NOT do, on purpose: it selects no power, enforces no duty cycle and models no
  board. The datasheet restricts +20 dBm to a transmit duty cycle ≤ 1 %, VSWR ≤ 3:1 and VDD 2.4–3.7 V
  (SX1276/7/8 §5.4.3); the daemon prints that contract once at startup and one line per accepted
  `POWER=20`, and the operator is responsible for it. On the LoRaHAM board the 433 module is an
  amplified RFM98PW whose documentation does not specify this drive condition — unvalidated there.
- `GET STATUS` gains two additive fields at the end of the line: `HIGHPOWER=0|1` (the raw boot flag)
  and `CHIPFAMILY=SX127x|SX1262` (the running process's resolved family), so a controller can show
  what the daemon *is* rather than what it stored — a hardware setup saved after the process started
  is not what is running.

## 1.1.1

- Chat waits for its transmission to finish before retuning to RX. With the shipped default of
  different TX and RX frequencies (433.775 / 433.900) the old fixed 100 ms sleep retuned while a
  queued frame was still waiting for a clear channel or in the air; the retune was lost and the
  node stayed on the TX frequency, deaf to replies. It now waits for the daemon's count of
  finished transmissions (TXOK+TXERR+TXBUSY) to move. No daemon behaviour changes; the daemon
  reports the version that is tagged.

## 1.1.0

- `GET CHANNEL NOSCAN`: the `CHANNEL` line without a channel-activity scan. `GET CHANNEL` runs a
  CAD scan — it takes the radio mutex, puts the chip into CAD and re-arms RX — so a client that
  polls it destroys frames that are arriving. Measured on a Zero 2 W at SF12/BW125, a ~0.58 Hz
  poll cost 46 % of delivered frames (54 % delivery, against 100 % with the poll stopped); the
  loss scales with airtime, so it is worst on the longest-range settings. The new command reports
  the same fields — `RSSI`, `PACKETRSSI`, `LIVERSSI`, `MODE`, `TXMODE` — with `CAD=0 CADSCAN=0`
  and `CADSTATE=NOTSCANNED`, or `PENDING` when a received packet is undrained, since that state is
  strictly more informative. `CADSCAN=0` means no verdict was taken, so `CAD=0` must not be read
  as "the channel is free". It answers with an absent or unready radio.
- `GET CHANNEL` is unchanged, and listen-before-talk is untouched: the MANAGED-TX gate still runs
  its own CAD before every transmission. What changes is that a *status reader* no longer has to
  pay for a verdict it did not ask for.

## 1.0.0

The reliability run, and the first release the maintainer calls 1.0. Seven
repairs from an external hardware audit, three review rounds on top of them,
and three hygiene changes. Proven on two boxes and on the air; the open item is
recorded at the end.

Why 1.0 rather than 0.11: what this release removed was not a set of bugs but a
set of *assumptions* — that a pin reports a CAD result, that a GPIO error is a
logic level, that a configured power level reaches the antenna, that a 200-byte
FSK write fits a 64-byte FIFO, that a log line an operator reads is in their
language. The daemon now fails closed where it used to guess, and says so
through exit codes and `RADIO=FAILED` rather than through silence.

Breaking:
- `POWER` on SX127x boards is now `2`–`17` dBm, not `0`–`20`. Below `2`,
  RadioLib drives the **RFO** pin instead of PA_BOOST — a different output path,
  and not the one the antenna is on, so `POWER=0` meant "transmit into an
  unconnected pin" while reporting success. Above `17`, RadioLib itself rejects
  `18` and `19`, and `20` is declined deliberately because its +20 dBm path
  carries a duty-cycle contract this daemon does not enforce. SX1262 keeps
  `0`–`20`.
- FSK frames on SX127x are limited to **63** payload bytes. The FIFO is 64 and
  variable-length mode puts the length byte in it. Oversized framed frames are
  rejected as `INVALID_PACKET` before anything is queued or the radio is
  touched; raw DATA input is chunked to the limit instead. LoRa keeps 255.
- **All log output is English**, and every line on stdout *and* stderr now
  begins with a UTC timestamp (`2026-09-16T08:22:31.003Z `). Anything grepping
  the old German strings must be updated; the CONF wire protocol, its keys and
  values, and the RF-log format are unchanged.
- Exit `4` now means "startup prerequisite unavailable — lock infrastructure
  **or** GPIO", not lock infrastructure alone. Exit `5` is renamed to
  `LORAHAM_EXIT_RUNTIME_RADIO_IO_ERROR` and logs `[RADIO] FATAL`, since GPIO
  failures exit through it too; the number is unchanged and the old macro name
  remains as an alias.

Radio behaviour:
- **CAD is read from the chip's registers instead of a pin.** RadioLib's SX127x
  `scanChannel()` waits on DIO0 and polls DIO1, so a board that does not route
  DIO1 could never see `CadDetected` and reported a free channel for every scan.
  The driver now polls the latched `CadDone`/`CadDetected` bits in `RegIrqFlags`
  against a deadline computed from the current SF/BW. **Uputronics therefore has
  real listen-before-talk and reports `CADSCAN=1`**; it previously fell back to
  the RSSI threshold.
- A CAD that never completes is an error, not a verdict. It ends the transmit
  attempt and can never be converted into a send by `CADTXAFTERTIMEOUT`.
- A received packet is no longer destroyed by a channel probe. The probe stops
  the receiver, checks both its own flag and the chip's latched `RxDone`, and
  abandons the scan if either says a packet is waiting. A pending packet also
  ends a synchronous MANAGED transmit immediately rather than being carried into
  the timeout decision.
- `GET CHANNEL` outside LoRa, or while an RX re-arm is pending, answers
  `CADSCAN=0 CADSTATE=UNAVAILABLE` **before touching the radio**. It still
  reports an RSSI in FSK, now through the skip-receive read — the ordinary one
  re-enters RX and clears every IRQ flag.
- `READY` now means configured radio **+ usable IRQ path + armed RX**. It was
  previously set before the packet-received callback was installed, whose result
  was discarded, so a radio whose IRQ line was never claimed could report READY
  and stay silent.
- A GPIO call that fails can no longer look like a pin level. RadioLib returned
  the error code through a `uint32_t`, making it read as a logic HIGH — which
  ended `transmit()`'s wait immediately and returned success, and told
  listen-before-talk the channel was clear.
- `SET LDRO=AUTO` now writes the register. RadioLib's `autoLDRO()` only sets a
  flag, so on a board without a RESET line a stale bit survived a restart while
  the command reported success.
- Output power and the PA over-current limit are one setting, re-applied at
  boot, on every `SET POWER` and after every modem switch. RadioLib pins OCP to
  60 mA inside both `begin()` and `beginFSK()` — below the 87 mA the PA draws at
  +17 dBm — so protection could trip during ordinary transmission. The daemon
  sets **100 mA, the chip's silicon default**.

Notes for operators:
- Do not poll `GET CHANNEL` back to back: the probe declines rather than waiting
  when the TX worker holds the radio or a packet is undrained.
- On a board with no RESET line, `READY` does not prove the antenna path works.
  A receiver can come up deaf after repeated restarts; a `SET MODE=FSK` then
  `SET MODE=LORA` round trip recovers it without a power cycle. See
  `docs/hardware.md`.
- FSK between two crystal-referenced boards is less forgiving than LoRa. If a
  link is poor where LoRa is fine, widen `RXBW` on the receiving side.

Open:
- The 100 mA OCP value is the documented silicon default, not a measured one. No
  current meter was available. If measurement shows +17 dBm into a real mismatch
  needs more headroom, the number should rise.

## 0.10.0

- RF log: `--rflog on|off --rflog-path <absolute>` appends one line per RF frame the
  radio received (with RSSI/SNR) or transmitted (with outcome, after `transmit()`
  returned OK — a CAD refusal or radio error never radiated and is not logged).
  Raw payload as hex and ASCII. Capped at 5 MB by copy-truncate to `<path>.1`; the
  inode never changes, so an external truncate is tolerated. `on` without a path,
  or an unopenable path, refuses to start rather than silently not logging.
- `docs/data-protocol.md`: framed RX frames carry RSSI/SNR in their header; the
  "payload only" sentence was stale.

## 0.9.0

**This entry is where the version scheme changes.** Everything before it was a plain
counter, and `112` was its last value; from here the daemon uses semantic versions.
Nothing about its behaviour changes with this entry.

- Repository restructured: the reference documentation moved out of the daemon's
  README into `docs/`, one subject per page; the client programs into `clients/`;
  the single-file daemons into `archive/`. `loraham_daemon/README.md` is now what
  the daemon is and how to build and run it.
- Every factual claim in the old manual was checked against the source. The
  documentation now describes what the code does -- socket ownership and modes,
  exit codes including the restartable runtime SPI failure, the raw DATA stream
  contract, the SX1262 parameter rasters, and the `--hw` presets.
- `test_multi_instance` no longer reports SKIP when a daemon fails to start on a
  machine that has radio hardware; it checks for the hardware first and fails
  otherwise, like the other live-daemon tests.
- CI runs each ref in its own concurrency group.

## loraham_daemon 112

Breaking:
- `--radio 433|868` is now mandatory (no `--radio both` / implicit default); dual-band runs two processes.
- Band-suffixed flags removed (`--tx-mode-433/868`, `--cad-monitor-433/868`, `--cad-rssi-433/868`); the plain flags apply to the selected band.
- CONF now sends one reply per command: `OK` or `ERR MALFORMED|UNKNOWN|INVALID|BUSY|RADIO_NOT_READY|HARDWARE` (`GET STATUS/STATS/CHANNEL` keep their data line, no trailing `OK`). Parsers must consume it.
- Public sockets moved to `/run/loraham` under systemd; direct/user runs use `LORAHAM_SOCKET_DIR=/tmp`. Bundled clients auto-detect both.
- Daemon runs unprivileged as the `loraham` user (hardware via spi/gpio groups); `GET STATUS` gains `RXREADY`, `GET STATS` gains `RXREARMFAIL`.

Behavior:
- `SET FREQ` is rejected off the selected band (433: 430–440 MHz, 868: 863–870); slow configs are rejected by a 20 s worst-case airtime limit.
- `SET TXQUEUE=0` and radio-touching `SET`s are rejected (`ERR BUSY`) while the TX queue is draining.
- `SET MODE` re-applies the band's boot RF defaults; boot fails closed if any RF setter or RX-arm fails; a persistently deaf receiver escalates to `RADIO=FAILED`.
- MANAGED TX aborts on a failed/unavailable CAD probe instead of treating it as free; a pending RX packet is no longer destroyed by a CAD probe or a rejected CONF line.
- Per-process GPIO/SPI locks fail the boot closed on conflict; startup lock failures exit 4 (no restart), runtime SPI fatals exit 5 (restartable), duplicate instance exits 3.
- SX1262: all `OOK`, `ENCODING=1`, and `FREQDEV<0.6` rejected at prevalidation; RF-switch failure now fails closed.

New hardware:
- Uputronics Raspberry Pi Zero LoRa(TM) Expansion Board V2.5C.
- Waveshare SX1262 LoRaWAN/GNSS HAT.

## loraham_daemon 111a
- loraham_daemon can now safely run multiple instances
- CAD monitor: fixed lost `CAD=0` when RX was pending; free now confirms after 2 samples 3 dB below `CADRSSI`

## loraham_daemon 111

- CAD/TX was reworked: MANAGED TX now performs bounded CAD/LBT with a stable
  idle window and returns `CHANNEL_BUSY` on timeout (default 1.5 s) instead of
  transmitting, while DIRECT TX transmits immediately with no CAD gating
  (selectable at boot per band via `--tx-mode`/`--tx-mode-433`/`--tx-mode-868`
  or at runtime via `SET TXMODE`); legacy/raw clients need `DIRECT` for
  backward compatibility. Per-band CAD policy
  (CADWAIT/CADIDLE/CADPOLL/CADTXAFTERTIMEOUT) is now configurable via CONF and
  reported in `GET STATUS`.
- DATA TX now runs through a bounded async queue, enabled by default
  (`SET TXQUEUE=0` keeps the direct path). Daemon-owned worker threads own the
  CAD/LBT decision, queued jobs retain their CAD callback, and completions are
  delivered in the main loop as final `TX_RESULT` frames with preserved sequence
  numbers and client-slot generation checks.
- RadioLib access now uses a per-radio guard across TX, RX, CAD, RSSI,
  monitoring, and CONFIG apply. CAD probes restore LoRa RX after scanning, and
  CAD monitoring uses a dedicated broadcast latch, scanning at most every 200 ms
  and only for subscribed CONF clients.
- Status and framed protocol were extended: `GET CHANNEL` reports per-band
  radio/CAD/RSSI/mode/TXMODE snapshots (LIVERSSI, CADSCAN, CADSTATE, PACKETRSSI)
  and returns immediately during active TX; framed `TX_RESULT` is configurable
  per band; STATUS exposes queue and completion counters including
  `TXQRESULTDROP`.
- Shutdown now discards queued TX jobs while letting only the in-flight job
  finish and rejects jobs submitted after shutdown begins. Async completion
  statistics use an atomic worker-to-runtime handoff and stay in sync with
  `GET STATS`/`GET STATUS`.
- Build, test, and operational hardening: release builds are hardened and CI
  runs pinned-RadioLib normal/strict/ASan-UBSan/TSan suites with TX-worker
  stress coverage; sockets use 0660 with a restrictive daemon umask; epoll
  supports 128 FDs and stops cleanly on registration errors; the per-band
  activity LED is driven by a single derived-state writer (transmit or
  CAD/channel busy) so it can no longer latch on, with partial-claim cleanup.
  README documents the current TX/CAD/queue/completion/CONF interfaces.
- Replaced per-loop epoll reset/re-registration with persistent watch
  reconciliation, dynamic EPOLLOUT interest, stale-watch cleanup, FD-reuse
  protection, and regression coverage.


## loraham_daemon 110

- Framed DATA `RX_PACKET` now prepends RSSI/SNR metadata before RF bytes.
*Breaks data socket interface dompatibility with 109a*

## loraham_daemon 109a

- Refactor / Hardening
  - Modularized loraham_daemon main module
  - General code cleanup.

- Bugfixes
  - Record CAD timeouts through the TX result stats path.
  - Check daemon background-mode directory and stdio redirection failures.
  - Acquire TX busy state atomically before copying/logging payloads.
  - fd `0` client handling with explicit `-1` client-set initialization.

- New Features
  - CONF status: broadcast local TX state as `TX=1` / `TX=0`.
  - CONF status: add `GET STATUS` runtime snapshot reply.
  - Runtime stats: add hourly operator stats and `GET STATS` on CONF sockets.

## loraham_daemon 109

Refactoring by Johannes Loose
Initial version: loradaemon_320_108 by Alexander Walter

- Refactor / Hardening
  - Event loop: moved polling/socket loop toward event-backend structure with test coverage.
  - Socket handling: hardened client slots, nonblocking I/O, queued broadcasts, and slow-client behavior.
  - RadioController/OOP: replaced radio globals and centralized 433/868 runtime state in `RadioController`.
  - Radio lifecycle: moved RadioLib objects to `std::unique_ptr` ownership with explicit shutdown.
  - TX path: routed DATA-TX, CAD guard, band/mode/health, and send flow through `RadioController`.
  - RX path: structured RX packet flow, IRQ/FIFO handling, and broadcast path through `RadioController`.
  - CAD/RSSI: routed polling and RSSI streaming through controller state instead of legacy mirrors.
  - CONFIG path: CONFIG dispatch now uses `RadioController` as runtime/hardware source.
  - TX logging: show compact TX packet preview in normal output.
  - TX logging: print full normal-mode ASCII packet preview.
  - LED path: radio-flow LED handling now goes through `RadioController`.
  - Logging module: extracted logger implementation into `daemon_log.h/.cpp`.
  - Test hardening: expanded structural guards and integration/regression coverage in `run_tests.sh`.
  - Harden RX packet handling: validate RX lengths, drop invalid packets safely, and avoid parsing short LoRa payloads as metadata.
  - Harden CONFIG command validation: require exact SET commands and reject unknown CONFIG keys.
  - Harden CONFIG apply: abort remaining changes after failed MODE switch and defer GETRSSI until mode switching succeeds.

- Bugfixes
  - Fix CONFIG stream framing: fragmented commands are buffered and newline-separated commands are processed individually.
  - Fix client broadcast errors: failed writes close broken clients instead of keeping stale slots.
  - Fix client slot overflow: accepted clients without a free slot are closed instead of leaked.
  - Fix FSK SHAPING parsing: BT values now map to RadioLib constants instead of truncating to 0.
  - Fix TX bounds checks: invalid or oversized packets are rejected before copy/transmit.
  - Fix RX error forwarding: RadioLib CRC/header/read errors are dropped and counted.
  
- New Features  
  - Build Script `./build.sh`  
  - Testsuite `./run_tests.sh`  
  - Debug logging`--debug`  
  - Added `--help`  
  - Version now lives in `daemon_version.h`
  - Added one-radio-mode --radio 433 | 868 | both
  - Add framed DATA sockets with packet-boundary-preserving TX/RX frames, shared-radio behavior 
