# Limits and timing

This page collects the daemon's public limits, its timing constants, and the TX/CAD, startup and
shutdown behaviour built on them. Every value below says whether it is a compile-time constant or
settable at runtime. The commands that change the settable ones are described in
[CONF protocol](conf-protocol.md), the boot-time flags in [CLI](cli.md), and the frame
layouts the size limits apply to in [DATA protocol](data-protocol.md).

## Contents

- [Size and capacity limits](#size-and-capacity-limits)
- [Timing constants](#timing-constants)
- [Runtime-settable CAD and TX parameters](#runtime-settable-cad-and-tx-parameters)
- [TX modes and the TX queue](#tx-modes-and-the-tx-queue)
- [CAD probes](#cad-probes)
- [The CAD monitor indicator](#the-cad-monitor-indicator)
- [Accepted parameter ranges](#accepted-parameter-ranges)
- [Worst-case airtime ceiling](#worst-case-airtime-ceiling)
- [Startup and shutdown timing](#startup-and-shutdown-timing)

## Size and capacity limits

All values in this table are compile-time constants. None of them can be changed at runtime or
from the command line.

| Constant | Value | Meaning |
|---|---:|---|
| `MAX_CLIENTS` | `10` | Client slots per socket group; there are three groups (raw DATA, framed DATA, CONF), each with its own slot array |
| `buf_SIZE` | `256` bytes | Internal RX buffer and CONF read scratch buffer; it is also the bound on a single CONF request line |
| `DATA_TX_MAX_CHUNK_SIZE` | `255` bytes | Ceiling of the raw DATA chunker; the chunk actually generated is the radio's current limit (below) |
| `FRAMED_DATA_MAX_RF_PAYLOAD` | `255` bytes | Storage ceiling for `TX_PACKET` / `RX_PACKET`; what the radio will accept is the limit below |
| `DAEMON_TX_QUEUE_CAPACITY` | `8` jobs | Per-radio bounded async TX queue |
| `DAEMON_TX_COMPLETION_QUEUE_CAPACITY` | `16` results | Per-band bounded async TX completion queue |
| `LORAHAM_SPI_LOCK_TIMEOUT_MS` | `2000` ms | Bound on acquiring the shared SPI transaction lock |
| `CONFIG_POLICY_MAX_AIRTIME_MS` | `20000.0` ms | Worst-case single-packet airtime a configuration may have |

### RF payload limit

The constants above are **storage** ceilings. What one frame may actually carry depends on the chip
family and the modem:

| Board / mode | Limit |
|---|---:|
| SX127x, LoRa | `255` bytes |
| SX127x, FSK | `63` bytes |
| SX126x, either | `255` bytes |

The SX127x FSK FIFO is 64 bytes and variable-length packet mode puts the length byte into it, so 63
is what fits. A `TX_PACKET` above the limit is rejected as `INVALID_PACKET` before anything is
queued and before the radio is touched; raw DATA input is chunked to the limit instead. The limit
also sets the byte bound on how much is read from a client at once, so the chunker cannot generate
more jobs than the TX queue has room for.

The two queues overflow in opposite directions, and a client that watches only one of them will
draw the wrong conclusion from the other:

- The TX queue is **reject-newest**. A submission into a full queue returns `-1`; no already
  queued packet is discarded.
- The completion queue is **drop-oldest**. A result pushed into a full queue advances the head,
  discarding the oldest stored result, and increments the eviction counter reported as
  `TXQRESULTDROP`.

## Timing constants

| Constant | Value | Meaning | Settable |
|---|---:|---|---|
| `DAEMON_TX_POLICY_BUSY_TIMEOUT_MS` | `120000` ms | How long a direct synchronous DATA TX waits for another TX to finish | Compile-time only |
| `DAEMON_TX_POLICY_CAD_WAIT_TIMEOUT_MS` | `1500` ms | How long `MANAGED` TX waits for the channel | Boot default; `SET CADWAIT` |
| `DAEMON_TX_POLICY_CAD_IDLE_STABLE_MS` | `250` ms | Continuous idle CAD time `MANAGED` TX requires before transmitting | Boot default; `SET CADIDLE` |
| `DAEMON_TX_POLICY_POLL_INTERVAL_MS` | `50` ms | Poll interval of both the TX-busy and the CAD wait loop | Boot default; `SET CADPOLL` |
| `DAEMON_TX_POLICY_SEND_AFTER_CAD_TIMEOUT` | `0` (disabled) | Whether `MANAGED` TX sends anyway after the CAD wait timeout | Boot default; `SET CADTXAFTERTIMEOUT` |
| `DAEMON_EVENT_LOOP_TIMEOUT_USEC` | `10000` µs / `10` ms | Main loop socket wait timeout | Compile-time only |
| `DAEMON_RSSI_INTERVAL_MS` | `100` ms | `GETRSSI=1` stream cadence, about 10 Hz | Compile-time only |
| `DAEMON_CAD_POLL_INTERVAL_MS` | `200` ms | Cadence of the CONF `CAD=1/0` RSSI probe | Compile-time only |
| `RADIO_CAD_RSSI_BUSY_THRESHOLD_DBM` | `-90.0` dBm | Live RSSI at or above which the channel counts as busy | Boot default; `--cad-rssi`, `SET CADRSSI` |
| `DAEMON_CAD_FREE_HYSTERESIS_DB` | `3.0` dB | How far below the threshold a sample must be to confirm free | Compile-time only |
| `DAEMON_CAD_FREE_CONFIRM_SAMPLES` | `2` samples | Consecutive confirming samples before `CAD=0` is published | Compile-time only |
| `DAEMON_RX_REARM_FAIL_LIMIT` | `30` failures | Consecutive failed re-arms that flip the radio to `FAILED` | Compile-time only |
| `DAEMON_RX_REARM_RETRY_MS` | `1000` ms | Backoff between RX re-arm attempts, at most one per second | Compile-time only |
| `DAEMON_STATS_LOG_INTERVAL_MS` | `3600000` ms / 60 min | Cadence of the compact operator stats line | Compile-time only |

`DAEMON_STATS_LOG_INTERVAL_MS` is a fixed constant, not a default: there is no command-line option
and no runtime setter for the stats cadence. Earlier documentation described it as changeable,
which was never true.

## Runtime-settable CAD and TX parameters

These are per band, and each takes effect for the process that owns that band. The command syntax
and the reply contract are in [CONF protocol](conf-protocol.md).

| Setting | Accepted range | Default |
|---|---|---|
| `SET CADWAIT=<ms>` | `50`–`5000` ms | `1500` ms |
| `SET CADIDLE=<ms>` | `0`–`2000` ms | `250` ms |
| `SET CADPOLL=<ms>` | `10`–`500` ms | `50` ms |
| `SET CADRSSI=<dbm>` | `-130`–`0` dBm | `-90` dBm |
| `SET CADTXAFTERTIMEOUT=<0\|1>` | `0` or `1` | `0` |
| `SET CADMONITOR=<0\|1>` | `0` or `1` | `0` |
| `SET TXMODE=DIRECT\|MANAGED` | `DIRECT` or `MANAGED` | `MANAGED` |
| `SET TXQUEUE=<0\|1>` | `0` or `1` | `1` |

`CADRSSI` is the busy threshold of the **passive** RSSI mechanism: the `CAD=0/1` monitor, and the
degraded path a profile would take if it declared no trustworthy active CAD. Since the SX127x
driver polls `RegIrqFlags` for the CAD verdict, every current preset has `CADSCAN=1` — including
Uputronics, which routes no DIO1 — so `CADRSSI` no longer gates whether `MANAGED` TX transmits.
See [CAD probes](#cad-probes).

## TX modes and the TX queue

The default TX mode is `MANAGED`, and the default DATA TX path uses `TXQUEUE=1`. DATA socket
transmissions therefore enter the per-band bounded async worker queue, and CAD/LBT runs inside
that worker before the RF transmit. `SET TXQUEUE=0` keeps the direct DATA TX path.

`MANAGED` waits for the stable-idle CAD window before transmitting. If the CAD wait timeout
expires first, the send returns `CHANNEL_BUSY` and the packet is discarded, unless
`CADTXAFTERTIMEOUT=1` is set, in which case it sends anyway.

`DIRECT` applies no CAD gating: the direct path returns a free verdict without probing, and the
queued path disables CAD for the job entirely. `DIRECT` therefore never returns `CHANNEL_BUSY`
*from CAD gating*. It can still return `CHANNEL_BUSY` from the paths that have nothing to do with
CAD:

| Situation | Outcome | Notes |
|---|---|---|
| `TXQUEUE=1`, async queue full | `CHANNEL_BUSY` | Logged and counted as `TX_RESULT_BUSY`; increments `TXQREJECT` |
| `TXQUEUE=0`, wait for the radio to leave TX-busy times out after `120000` ms | `CHANNEL_BUSY` | Logged and counted as `TX_RESULT_BUSY` |
| `TXQUEUE=0` with residual async work still pending or executing | `BUSY` | Defence in depth: a direct TX must not wait behind or interleave with residual jobs |

The 120 s TX-busy timeout does not produce the framed status `BUSY`. It returns the outcome
`CHANNEL_BUSY`, which maps to framed status `CHANNEL_BUSY` (`2`), not `BUSY` (`1`); only the stats
counter and the log line say `BUSY`. The framed status `BUSY` (`1`) comes from the residual-queue
guard.

A drop-free `DIRECT` needs both settings: `--tx-mode direct` (or `SET TXMODE=DIRECT`) *and*
`SET TXQUEUE=0`. Under the queued default a `DIRECT` job is still submitted to the 8-slot queue
and is rejected when that queue is full.

Older or raw clients open a DATA socket, write, and never issue `SET TXMODE`. Under the `MANAGED`
default their packets can be delayed, or dropped on a CAD timeout. Start such a band in `DIRECT`
with `--tx-mode direct` so send-when-told behaviour is active from boot.

## CAD probes

The daemon has two distinct channel probes.

**Active scan probe.** Calls `scanChannel()` on the driver. It is used only for `MANAGED` TX
gating and for the on-demand `GET CHANNEL` query. Two guards apply:

- An RF packet that finished reception but has not been drained by the main loop yet makes the
  probe report `BUSY` without scanning, so the probe's IRQ-clear and RX re-arm can never destroy a
  pending packet. This is checked **twice**: once against the daemon's own `received` flag, and
  again against the chip's latched `RxDone` immediately before the probe detaches DIO0 and enters
  CAD. The second check is the one that counts — `received` is set by the lgpio alert thread, which
  does not hold the radio mutex, so a packet can complete between the first check and the
  transition, and the chip's flag does not depend on when a thread is scheduled.
- Outside LoRa, and while an RX re-arm is pending, the active probe returns `UNAVAILABLE` **before
  any radio call**. In FSK it still reports an RSSI, but through the skip-receive read: the
  ordinary `getRSSI()` re-enters RX in FSK, which rewrites the DIO mapping and clears every IRQ
  flag, and that is exactly what must not happen here.
- A profile that declared no trustworthy active CAD would fall through to the passive probe, so
  `MANAGED` TX gating would degrade to listen-before-talk against the `CADRSSI` threshold. No
  current preset does: the SX127x driver polls `RegIrqFlags` and needs no DIO1.

**Passive RSSI probe.** Reads the live channel RSSI from the driver only. It never changes radio
mode, never calls `scanChannel()`, and never re-arms RX, so it cannot disturb continuous RX. It is
skipped rather than blocked: it returns `UNAVAILABLE` when TX is busy or when the radio mutex is
not free on a try-lock, and `UNAVAILABLE` means the state was left untouched. The verdict is
`BUSY` when the RSSI is at or above the `CADRSSI` threshold and `FREE` below it. Outside LoRa mode
it returns `UNAVAILABLE`.

## The CAD monitor indicator

The `CAD=1` / `CAD=0` lines on the CONF socket come from the passive probe, not from scan-based
CAD.

- Monitoring runs only when it is opted in per band with `SET CADMONITOR=1` *and* a CONF client is
  connected. Without the opt-in, or with no CONF client, nothing is sampled.
- It runs in LoRa mode only. In FSK the passive probe returns `UNAVAILABLE` and no `CAD` line is
  produced.
- A sample is taken at most every `200` ms.
- `CAD=1` asserts immediately at or above the threshold (default `-90` dBm).
- `CAD=0` is published only after `2` consecutive samples at least `3.0` dB below the threshold,
  about 400 ms at the 200 ms cadence.
- A sample in the dead band between the two levels keeps the currently published state and cancels
  an incomplete free confirmation.
- A pending or newly received RF packet never suppresses or delays the `CAD=0` edge: the monitor
  tick ignores the received flag.

## Accepted parameter ranges

A `SET` outside these ranges is rejected before any hardware is touched.

| Parameter | Accepted values |
|---|---|
| `FREQ` (`--radio 433` process) | `430.0`–`440.0` MHz |
| `FREQ` (`--radio 868` process) | `863.0`–`870.0` MHz |
| `SF` | `7`–`12` |
| `BW` | `7.8`, `10.4`, `15.6`, `20.8`, `31.25`, `41.7`, `62.5`, `125`, `250`, `500` kHz |
| `CR` | `5`–`8` |
| `PREAMBLE` (LoRa) | `6`–`512` symbols |
| `POWER` (SX127x board) | `2`–`17` dBm, plus exactly `20` with `--high-power` |
| `POWER` (SX126x board) | `0`–`20` dBm |
| `BR` (FSK) | `0.5`–`300` kbps |
| `FREQDEV` (FSK) | greater than `0` and at most `200` kHz; on an SX126x board additionally at least `0.6` kHz |

A frequency outside the band window is rejected with the distinct reason
`off-band frequency (band policy)`.

The `POWER` window is narrower than the hardware, and it is per chip family. The policy check runs
before `setOutputPower()`, inside the whole-command prevalidation, so a refused value has no
hardware effect and takes the keys beside it down with it.

On an **SX126x** board the chip covers -9 dBm to +22 dBm and the accepted range `0`–`20` dBm lies
inside it. The `--high-power` flag is accepted on such a process and changes nothing.

On an **SX127x** board the continuous range is `2`–`17` dBm, for two separate reasons:

- Below `2` dBm RadioLib drives the **RFO** pin instead of PA_BOOST. That is a different output
  path, and it is not the one the antenna is connected to on these boards, so `POWER=0` would have
  meant "transmit into an unconnected pin" while reporting success.
- `18` and `19` dBm are rejected by the pinned RadioLib itself (`checkOutputPower` accepts `2`–`17`
  on PA_BOOST and special-cases exactly `20`). That is an API boundary, not a silicon one — Semtech's
  reference driver reaches them through the boosted PA — but this daemon does not go around the
  library; rejecting them here only makes the error early and specific instead of a late driver code.
  The `--high-power` permission does not admit them.

### +20 dBm: the opt-in and its contract

Exactly `20` dBm is the datasheet's high-power mode (`RegPaDac` 0x87, SX1276/77/78/79 §5.4.3), and
the datasheet restricts it: a transmit duty cycle of at most **1 %**, a VSWR of at most **3:1** at the
antenna port, and VDD **2.4–3.7 V**, over −40…+85 °C. Nothing in this daemon measures or enforces
any of that — there is **no duty-cycle governor**, by decision — so `20` is admitted only when the
operator started the process with `--high-power` ([cli.md](cli.md)), the explicit acknowledgement of
the contract. The permission is per process, immutable, and no `CONF` command can grant it; without
it a `SET POWER=20` is refused with the logged reason `high-power mode not enabled (start with
--high-power)`. The daemon prints the contract once at startup and one line per accepted `POWER=20`;
the operator is responsible for it. A 1 % duty cycle is arithmetic on airtime: a frame of airtime
*T* seconds repeated every *P* seconds needs *P* ≥ 100 *T*, and the airtime depends on payload,
SF, BW, CR, preamble and header, not on SF/BW alone — the daemon does not compute a "safe
interval" for you.

**The board matters and the daemon does not model it.** `POWER` is the chip's drive. On a bare
module (the Uputronics RFM95/98W) +20 dBm is the datasheet case. On the LoRaHAM board the 433 module
is an amplified **RFM98PW** whose documentation does not specify this drive condition or the
resulting module output; the permission is the same switch there, and that operation is
**unvalidated** — see [hardware.md](hardware.md).

Output power and the PA over-current limit (OCP) are applied together as one setting, and the limit
follows the level. RadioLib pins OCP to 60 mA inside both `begin()` and `beginFSK()`, below the
datasheet typical draw of 87 mA at +17 dBm on PA_BOOST; the daemon sets it to **100 mA — the chip's
own silicon default** — for `2`–`17`, at boot, on every `SET POWER`, and after every LoRa/FSK switch,
because `beginFSK()` re-pins it.

At `20` the pair is **140 mA (OcpTrim 17)** with the boosted `RegPaDac`, written in the same apply.
The datasheet says the limit "should be adapted to the actual power level" and gives 120 mA as the
typical total draw at +20 dBm; `Imax` bounds the PA current only, so that typical does not translate
into an exact PA figure, but 100 mA sits at or below the documented +20 dBm region and is likely to
limit the amplifier there, and RadioLib's 60 mA certainly does. 140 mA is above that region with
headroom and is the pairing the Arduino-LoRa library has shipped for years (140 above 17 dBm, 100
below). It is a project choice, not a Semtech-prescribed number, and it has not been current-measured
on these boards — the same evidence status as the 100. Leaving `20` — a lower `SET POWER`, a `MODE`
switch (which reloads the band's boot defaults), a restart — restores 0x84 and 100 mA; the register
sequence is pinned by `tests/test_sx127x_power_register.cpp` against the real pinned RadioLib.

The defect the 100 mA fixes is that RadioLib's 60 mA sits *below* the typical draw at +17, so the
protection can trip during ordinary transmission. Restoring the silicon default corrects that and
asserts no figure of the project's own: it leaves the part exactly as protected as an unconfigured
one. An earlier revision used 120 mA as a selected margin for temperature and VSWR, conditional on a
bench measurement; no current meter was available, so rather than ship an unmeasured number the
value is the documented default.

## Worst-case airtime ceiling

The daemon rejects any `SET` whose merged configuration would give a worst-case single-packet
airtime above `CONFIG_POLICY_MAX_AIRTIME_MS`, that is 20 s. Worst case means the largest payload the
daemon can actually send in the prospective mode, with CRC on: 255 bytes in LoRa, and 63 bytes in
FSK on an SX127x board, where the 64-byte FIFO also holds the length byte. The gate runs before any hardware side effect and logs the computed airtime together
with the rejection:

```
[<band>] CONFIG rejected: worst-case airtime <ms> ms > 20000 ms (SF<n>/BW<khz>/CR<n>/PRE<n>, <n> B)
```

The check is made against a merged shadow of the configuration, not against the single key being
set:

- In LoRa the shadow tracks `SF`, `BW`, `CR` and `PREAMBLE`; in FSK it tracks the bitrate and the
  preamble.
- A `MODE` token in the same command re-bases the shadow on the band's boot defaults.

That merge is what makes the outcome depend on the whole line. `SET SF=7 BW=7.8` passes at roughly
9 s, while `SET BW=7.8` alone under an SF12 configuration is rejected at roughly 145 s.

Both per-band boot profiles pass the gate: about 9.0 s at 433 (SF12/BW125/CR5, preamble 8) and
about 2.2 s at 868 (SF11/BW250/CR5, preamble 16).

The 20 s ceiling deliberately sits below the `TimeoutStopSec=30` of the shipped systemd unit, so
an in-flight packet cannot outlive a stop request. See [Deployment](deployment.md).

## Startup and shutdown timing

**SPI lock.** Every SPI transaction acquires the shared lock by polling `flock(LOCK_EX|LOCK_NB)`
at a 1 ms interval against a `CLOCK_MONOTONIC` deadline of `LORAHAM_SPI_LOCK_TIMEOUT_MS` (2000
ms). Expiry is a controlled fatal; the daemon never proceeds unlocked.

**RX re-arm.** A failed re-arm must not leave a radio that reports ready but hears nothing.

- At boot the check fails closed: a failed `startReceive()` marks the radio `FAILED` with the
  numeric RadioLib code.
- At runtime a failure is logged once per incident, increments the `rx_rearm_failures` counter and
  latches a pending flag; a later successful re-arm logs the recovery and clears the latch.
- Retries run in the main loop's radio tick, gated on TX-busy and a try-lock, at most one SPI
  attempt per second.
- `DAEMON_RX_REARM_FAIL_LIMIT` (30) consecutive failed re-arms flip the radio to `FAILED`.

**Operator stats.** Every 60 minutes the daemon prints one compact stats line for the selected
radio, carrying the same fields as `GET STATS`.

**Shutdown.** Stopping the async TX worker discards the jobs still queued, rejects any new
submission, and lets an already dequeued job run to completion before the thread is joined.
Cleanup then proceeds in a fixed order: radios stopped, event backend closed, client connections
closed, socket files removed, and the per-band instance lock released last, so a same-band restart
cannot bind sockets this instance is about to delete.
