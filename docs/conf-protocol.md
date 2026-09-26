# CONF protocol

The CONF sockets carry the daemon's text control protocol: `SET` commands, `GET`
queries, one reply per command line, and a few unsolicited broadcasts. Payload
transport is not part of this page — the raw and framed DATA wire formats live in
[data protocol](data-protocol.md), the socket paths and permissions in
[deployment](deployment.md), and the CAD/TX timing rationale behind the CAD keys
in [limits](limits.md).

## Contents

- [Command set](#command-set)
- [Line framing and parsing](#line-framing-and-parsing)
- [CONFIG parameters](#config-parameters)
- [Chip-family differences](#chip-family-differences)
- [Runtime setters](#runtime-setters)
- [Queries and reply lines](#queries-and-reply-lines)
- [Reply and error vocabulary](#reply-and-error-vocabulary)
- [Apply order and rejection rules](#apply-order-and-rejection-rules)
- [Unsolicited broadcasts](#unsolicited-broadcasts)
- [Examples](#examples)

## Command set

A CONF socket is an `AF_UNIX` / `SOCK_STREAM` listener. One process serves one
band, so every command applies to the band whose socket the client connected to.

```text
SET KEY=VALUE KEY=VALUE ...
SET TXRESULT=0|1
SET TXMODE=MANAGED|DIRECT
SET TXQUEUE=0|1
SET CADMONITOR=0|1
SET CADRSSI=<dbm>
SET CADWAIT=<milliseconds>
SET CADIDLE=<milliseconds>
SET CADPOLL=<milliseconds>
SET CADTXAFTERTIMEOUT=0|1
GET STATUS
GET STATS
GET CHANNEL
GET CHANNEL NOSCAN
```

`SET KEY=VALUE ...` carries one or more space-separated tokens for the radio
parameters in [CONFIG parameters](#config-parameters). The nine reserved
`SET` forms above are runtime setters that do not touch the radio; they are
matched as exact literals and are described in [Runtime setters](#runtime-setters).
`GET STATUS`, `GET STATS`, `GET CHANNEL` and `GET CHANNEL NOSCAN` are the only
recognised queries. **They do not cost the same.** `GET STATUS` and `GET STATS`
format cached fields and counters and never touch the radio. `GET CHANNEL` runs
a channel-activity scan: it takes the radio mutex, puts the chip into CAD and
re-arms RX afterwards, so it **destroys a frame that is arriving**. `GET CHANNEL
NOSCAN` answers the same line without ever scanning. See
[Which queries cost the radio](#which-queries-cost-the-radio).

## Line framing and parsing

- Input is line-framed: one newline-terminated line is one command. A `\r` is
  stripped, so `CRLF` clients work unchanged.
- A one-shot client that closes after a final unterminated command is still
  accepted: EOF flushes the partial line as a command.
- Keys are parsed case-insensitively — the parser uppercases every key, and the
  reserved-setter matchers uppercase the whole trimmed line. The uppercase
  spelling used throughout this page is canonical, not mandatory.
- Query commands are matched on a trimmed, uppercased copy, so leading and
  trailing whitespace and any case are accepted.
- Values are parsed strictly. Malformed suffixes, empty values, `nan`, `inf` and
  partial numbers are rejected. `SYNC` additionally accepts a `0x`/`0X` hex form.
- Malformed tokens such as `BROKEN`, `NOVALUE=` or `=BAD` reject the whole
  command; so does a duplicate key, including a repeated `MODE=`.
- Every complete command line receives exactly one newline-terminated response on
  the requesting connection.

## CONFIG parameters

Boot defaults are per band. `LORA`-only keys are ignored while the modem is in
FSK mode, and FSK-only keys are ignored while it is in LoRa mode; the key is
logged as ignored and the rest of the command still applies.

| Key | Mode | Default | Accepted values |
|---|---|---|---|
| `MODE` | global | `LORA` | exactly `LORA` or `FSK`; anything else is rejected as `unknown mode` |
| `FREQ` | LORA/FSK | 433: `433.900`, 868: `869.525` | strict number in MHz inside the band policy — `430.0`–`440.0` in a 433 process, `863.0`–`870.0` in an 868 process; an off-band value is rejected with the reason `off-band frequency (band policy)` |
| `POWER` | LORA/FSK | `10` | integer, per chip family: `2` to `17` dBm on SX127x — plus exactly `20` when the process was started with `--high-power` — and `0` to `20` dBm on SX126x (see `limits.md`) |
| `GETRSSI` | global | `0` | exactly `0` or `1` |
| `PREAMBLE` | LORA/FSK | 433: `8`, 868: `16` | LoRa: integer `6` to `512`; FSK: integer `0` to `2048`; the airtime gate applies on top |
| `SYNC` | LORA/FSK | 433: `0x12`, 868: `0x2B` | LoRa: `0x00`–`0xFF` or decimal `0`–`255`; FSK: one or two bytes up to `0xFFFF`, no zero byte |
| `SF` | LORA | 433: `12`, 868: `11` | integer `7` to `12` |
| `BW` | LORA | 433: `125`, 868: `250` | exactly `7.8`, `10.4`, `15.6`, `20.8`, `31.25`, `41.7`, `62.5`, `125`, `250`, `500` kHz |
| `CR` | LORA | `5` | integer `5` to `8` (`4/5` to `4/8`) |
| `CRC` | LORA | `1` on both bands | exactly `0` or `1` |
| `LDRO` | LORA | 433: forced `1`, 868: `AUTO` | exactly `AUTO`, `auto`, `0` or `1` |
| `BR` | FSK | see [Chip-family differences](#chip-family-differences) | strict number `0.5` to `300.0` kbps |
| `FREQDEV` | FSK | see [Chip-family differences](#chip-family-differences) | strict number `>0` to `200.0` kHz; SX1262 additionally requires `0.6` kHz or more |
| `RXBW` | FSK | see [Chip-family differences](#chip-family-differences) | a value from the active chip family's raster — the two rasters differ, see below |
| `OOK` | FSK | RadioLib default | SX127x: exactly `0` or `1`; SX1262: every `OOK` key is rejected |
| `SHAPING` | FSK | RadioLib default | exactly `off`, `none`, `0.0`, `0.3`, `0.5`, `0.7`, `1.0`, case-insensitive |
| `ENCODING` | FSK | RadioLib default | SX127x: `0`, `1`, `2`; SX1262: only `0` and `2` |

The 868 boot default `LDRO=AUTO` is the descriptor field `ldro = -1`; the 433 default `1` forces
LDRO on regardless of the configuration.

`AUTO` is not only a flag. RadioLib's `autoLDRO()` sets an internal flag and writes no register,
and RadioLib writes only on a cache difference — so on a board without a RESET line (Uputronics)
a forced LDRO bit from a previous run survives in the chip while the cache assumes the power-on
state. `AUTO` therefore computes the value from the **current** `SF`/`BW`, writes it, and only then
re-enables RadioLib's automatic tracking, so the register is right immediately **and** follows
later `SET SF` / `SET BW` commands. An explicit `LDRO=0` or `LDRO=1` still wins and stays fixed.

LDRO is required once the symbol time reaches **16 ms** (`2^SF / BW`), inclusive — 32.8 ms at
SF12/BW125 needs it, 8.2 ms at SF11/BW250 does not. The same rule feeds the airtime gate.

## Chip-family differences

Prevalidation asks the active driver for its family (`RadioDriver::chipFamily()`)
and validates against that family's policy, so a value the concrete chip would
reject is refused before any hardware is touched.

**`RXBW` raster.** The two rasters have no value in common, so a value valid on
one family is rejected on the other.

| Family | Accepted `RXBW` values (kHz) |
|---|---|
| SX127x | `2.6`, `3.1`, `3.9`, `5.2`, `6.3`, `7.8`, `10.4`, `12.5`, `15.6`, `20.8`, `25.0`, `31.25`, `31.3`, `41.7`, `50.0`, `62.5`, `83.3`, `100.0`, `125.0`, `166.7`, `200.0`, `250.0` |
| SX1262 | `4.8`, `5.8`, `7.3`, `9.7`, `11.7`, `14.6`, `19.5`, `23.4`, `29.3`, `39.0`, `46.9`, `58.6`, `78.2`, `93.8`, `117.3`, `156.2`, `187.2`, `234.3`, `312.0`, `373.6`, `467.0` |

On an SX1262 board `RXBW=23.4` is accepted and `RXBW=25.0` is rejected with
`ERR INVALID`; on an SX127x board it is the other way round. The rejection
happens at prevalidation, so nothing in the command is applied.

**Other family rules.**

| Key | SX127x | SX1262 |
|---|---|---|
| `FREQDEV` | `>0` to `200.0` kHz | same, plus a `0.6` kHz minimum |
| `OOK` | `0` or `1` | every `OOK` key rejected, `OOK=0` included — the chip has no OOK modulator, so accepting a no-op setter would report success for a missing capability. The driver's own guard prints `(SX1262: OOK unavailable)` and returns `RADIOLIB_ERR_INVALID_MODULATION` |
| `ENCODING` | `0`, `1`, `2` | `0` and `2` only; `1` would silently enable whitening instead of Manchester |
| `CRC` | LoRa CRC off/on | mapped to SX126x CRC length `2` (on) or `0` (off), at boot and on every apply |

**FSK mode-switch baseline.** `MODE=FSK` does not land on the band's full boot
profile — only the frequency comes from the band:

| Family | What `MODE=FSK` sets |
|---|---|
| SX127x | `beginFSK(freq)`; `BR`, `FREQDEV`, `RXBW` and the rest stay at RadioLib's own defaults |
| SX1262 | `beginFSK(freq, 4.8, 5.0, 156.2, 10, 16, …)` — bitrate `4.8` kbps, deviation `5.0` kHz, `RXBW` `156.2` kHz, power `10` dBm, `16`-bit preamble |

The airtime shadow used by the gate baselines FSK at `4.8` kbps and `16`
preamble bits on both families.

## Runtime setters

These nine commands are matched as exact literals, take effect immediately, and
never touch the radio. They are classified before the radio-readiness gate, so
they answer the same way whether or not the radio is ready.

| Command | Accepted values | Default | Effect |
|---|---|---|---|
| `SET TXRESULT=0\|1` | `0`, `1` | `0` | Enable or disable per-band `TX_RESULT` emission (see [data protocol](data-protocol.md)) |
| `SET TXMODE=MANAGED\|DIRECT` | `MANAGED`, `DIRECT` | `MANAGED` | Select the per-band TX mode |
| `SET TXQUEUE=0\|1` | `0`, `1` | `1` | `1` routes DATA TX through the per-band bounded async TX queue; `0` keeps the direct DATA TX path |
| `SET CADMONITOR=0\|1` | `0`, `1` | `0` | Per-band opt-in for the smoothed RSSI-based `CAD=0/1` broadcast. Disabling also clears the free-streak counter and the published CAD latch |
| `SET CADRSSI=<dbm>` | integer `-130` to `0` | `-90` | Per-band busy threshold for the RSSI-based CAD indicator, i.e. the passive `CAD=0/1` monitor. It is **not** the Uputronics listen-before-talk substitute any more: since the SX127x driver polls `RegIrqFlags` for the CAD verdict, every preset has trustworthy active CAD and `CADSCAN=1`. The threshold would only gate MANAGED TX on a profile that declared no trustworthy active CAD, of which there is currently none |
| `SET CADWAIT=<ms>` | `50` to `5000` | `1500` | CAD wait timeout |
| `SET CADIDLE=<ms>` | `0` to `2000` | `250` | Stable-idle window |
| `SET CADPOLL=<ms>` | `10` to `500` | `50` | CAD poll interval |
| `SET CADTXAFTERTIMEOUT=0\|1` | `0`, `1` | `0` | Whether to transmit anyway after a CAD wait timeout |

An out-of-range or unparsable value answers `ERR INVALID` and leaves the previous
value in place — the matcher accepts only a fully valid line, so no store
happens. A structurally incomplete form (`SET CADWAIT`, `SET TXQUEUE=`) answers
`ERR MALFORMED`.

`SET TXQUEUE=0` is rejected with `ERR BUSY` while queued jobs are pending or one
is executing (drain before disable); `SET TXQUEUE=1` always succeeds. The `TXQ*`
counters keep reporting the worker's real state regardless of the `TXQUEUE`
flag.

CAD policy changes apply to future TX attempts. A queued TX job snapshots band,
TX mode, CAD policy and payload at submission time — but no RF configuration.

## Queries and reply lines

Each query is answered with its own data line on the requesting connection, and
never with a trailing `OK`. The reply carries no band identifier: `RADIO=` is
the radio health state, so the band is implied only by which socket the client
connected to.

### `GET STATUS`

One runtime snapshot line, in this exact field order:

```text
STATUS RADIO= TX= CAD= GETRSSI= TXRESULT= TXMODE= TXQUEUE= TXQ= TXQDROP= TXQREJECT= TXQSTALE= TXQRESULTDROP= TXQDONE= TXQLAST= TXQSEQ= CADWAIT= CADIDLE= CADPOLL= CADTXAFTERTIMEOUT= CADMONITOR= CADRSSI= RXREADY= HIGHPOWER= CHIPFAMILY=
```

A default snapshot reads:

```text
STATUS RADIO=READY TX=0 CAD=0 GETRSSI=0 TXRESULT=0 TXMODE=MANAGED TXQUEUE=1 TXQ=0 TXQDROP=0 TXQREJECT=0 TXQSTALE=0 TXQRESULTDROP=0 TXQDONE=0 TXQLAST=NONE TXQSEQ=0 CADWAIT=1500 CADIDLE=250 CADPOLL=50 CADTXAFTERTIMEOUT=0 CADMONITOR=0 CADRSSI=-90 RXREADY=1 HIGHPOWER=0 CHIPFAMILY=SX127x
```

| Field | Meaning |
|---|---|
| `RADIO` | `READY`, `FAILED` or `UNINITIALIZED` |
| `TX` | `1` while a local transmit is in flight |
| `CAD` | The published CAD broadcast latch. It reflects monitoring activity only — a transient TX or an on-demand CAD probe does not change it |
| `GETRSSI` | Live-RSSI stream active |
| `TXRESULT`, `TXMODE`, `TXQUEUE` | The runtime setters above |
| `TXQ` | Jobs currently pending in the async TX queue |
| `TXQDROP` | Pending jobs discarded at shutdown |
| `TXQREJECT` | Jobs rejected because the queue was full (reject-newest, no packet discarded) or submitted after shutdown began |
| `TXQSTALE` | Final framed results suppressed because the original client slot generation no longer matches |
| `TXQRESULTDROP` | Completion records evicted from the bounded completion queue (oldest evicted on overflow) |
| `TXQDONE` | Jobs processed |
| `TXQLAST` | Name of the last queued TX result, `NONE` when there is none yet |
| `TXQSEQ` | Sequence number of the last queued TX |
| `CADWAIT`, `CADIDLE`, `CADPOLL`, `CADTXAFTERTIMEOUT`, `CADMONITOR` | The current CAD policy |
| `CADRSSI` | The busy threshold, printed integer-rounded (`%.0f`), not as a float |
| `RXREADY` | `0` while a failed RX re-arm is being retried. `RADIO=READY RXREADY=0` means the daemon is temporarily deaf; persistent re-arm failure escalates the radio to `RADIO=FAILED` |
| `HIGHPOWER` | The raw `--high-power` boot flag of this process, `0` or `1`. It reports the permission, not the selected power (`POWER` is not echoed anywhere), and it is `1` on an SX1262 started with the flag even though the flag does nothing there — combine it with `CHIPFAMILY` |
| `CHIPFAMILY` | The running process's resolved chip family, `SX127x` or `SX1262`. Added so a controller reads the family off the daemon instead of trusting a hardware setup that may have been saved after the process started. Both fields are additive at the end of the line; a reader that does not find them is talking to an older daemon and must treat them as unknown, not as `0` |

### `GET STATS`

Counters since daemon start, in this exact field order:

```text
STATS UPTIME= RADIO= RX= RXBYTES= RXDROPS= TXOK= TXERR= TXBUSY= CADTIMEOUT= CADSEND= RXREARMFAIL=
```

`RXREARMFAIL` counts failed RX re-arms after a TX, a probe or a CONFIG apply — a
non-zero value means the retry path had to recover the receiver. The same fields
are printed to the daemon log as one operator stats line every `3600000` ms.

### `GET CHANNEL`

A one-shot channel probe:

```text
CHANNEL RADIO= BUSY= CAD= CADSCAN= CADSTATE= RSSI= PACKETRSSI= LIVERSSI= MODE= TXMODE=
```

`CAD` is the legacy scan flag and `RSSI` the legacy packet-RSSI field; `CADSCAN`,
`CADSTATE`, `PACKETRSSI` and `LIVERSSI` are the explicit fields. `MODE` is the
current modem mode, `TXMODE` the current TX mode.

`CADSTATE` reports `FREE`, `BUSY`, `UNAVAILABLE` or `PENDING`. Three cases answer
without running a scan:

| Situation | Answer |
|---|---|
| A transmit is in flight | `BUSY=1 CADSTATE=UNAVAILABLE`, returned immediately, no radio scan |
| A received packet has not been drained yet | `CADSTATE=PENDING CAD=0 CADSCAN=0`, and `BUSY` is `1` only if a TX is in flight or the live RSSI is at or above the `CADRSSI` threshold — an idle channel with a pending packet reports `BUSY=0`. The pending packet survives; the probe's IRQ-clear and re-arm never run |
| A profile without trustworthy active CAD | The answer comes from the passive RSSI probe, and `CADSCAN=0` marks the non-scan source |
| Outside LoRa, or while an RX re-arm is pending | `CADSCAN=0 CADSTATE=UNAVAILABLE`. `RSSI` and `PACKETRSSI` are still reported — the snapshot is a non-destructive register read — but no CAD verdict is produced from a modem or a receiver state that cannot support one |

The MANAGED-TX gate uses its own probe, whose pending-RX guard returns an
unconditional `BUSY` instead.

### `GET CHANNEL NOSCAN`

The same `CHANNEL` line, **never** running a channel-activity scan. This is what
any periodic or automatic reader must use — an RSSI meter, a noise-floor poller,
a settings read-back.

| Situation | `CADSTATE` |
|---|---|
| A received packet has not been drained yet | `PENDING` — the stronger, genuinely informative state wins |
| Otherwise | `NOTSCANNED` |

`CAD=0` and `CADSCAN=0` in **both** cases. Read them carefully:

- `CADSCAN=0` here means **no CAD verdict was taken**, not "the scan found nothing".
- `CAD=0` must therefore **not** be read as "CAD says the channel is free".
- `BUSY` is only the TX-busy / live-RSSI-versus-`CADRSSI` estimate, exactly as in
  the pending-packet case above.

`RSSI`, `PACKETRSSI`, `LIVERSSI`, `MODE` and `TXMODE` are reported as usual — the three RSSI fields
all carry the live reading here, exactly as the pending-packet answer has always reported them.

The live read is gated on `tx_busy` and taken with a *try*-lock, so this command **never blocks on
a transmission**: while the TX worker owns the radio it answers immediately with the `-200`
sentinel rather than waiting out the airtime. That is deliberate — it is the command clients poll,
and it is dispatched on the daemon's main loop. The command is also valid, and answers, with an
absent or unready radio.

Listen-before-talk is **unaffected**: the MANAGED-TX gate runs its own CAD before
every transmission regardless of what any client queries.

### Which queries cost the radio

| Query | Touches the radio? | Use on a timer? |
|---|---|---|
| `GET STATUS` | no | yes |
| `GET STATS` | no | yes |
| `GET CHANNEL NOSCAN` | no scan; passive register reads only | yes |
| `GET CHANNEL` | **yes — CAD scan, destroys an arriving frame** | **no** |

`GET CHANNEL` is for a deliberate, operator-invoked measurement. A client that
polls it pays for the verdict in lost receptions: measured on a Zero 2 W at
SF12/BW125, a ~0.58 Hz poll cost **46 %** of delivered frames (54 % delivery
against 100 % with the poll stopped). The cost scales with airtime, so it is
worst exactly where the link is longest-range.

**A tight `GET CHANNEL` poll can return `UNAVAILABLE` with `CADSCAN=0`.** The
active probe takes the radio mutex with a *try*-lock and declines rather than
waiting: if the TX worker holds the radio, or a received packet has not been
drained, the answer is "state untouched, skip this sample". Under rapid polling
those conditions are simply met more often — each probe leaves the receiver
re-armed and a packet may land between samples, and a queued transmission can
hold the radio for the whole of a long frame's airtime.

Requests on one CONF connection are handled in sequence by the main loop, so a
poll does not contend with *itself*; what it contends with is the TX worker and
the receive state. Leave a realistic gap between samples and the probe scans and
answers normally.

## Reply and error vocabulary

Every complete CONF command line receives exactly one newline-terminated reply
on the requesting client's connection. The broadcasts in
[Unsolicited broadcasts](#unsolicited-broadcasts) are separate and unaffected.

```text
OK                    accepted (runtime setter, applied CONFIG, or accepted no-radio-op)
ERR MALFORMED         malformed SET, malformed token, or SET without parameters
ERR UNKNOWN           unknown command or unknown key
ERR INVALID           valid syntax rejected by value, capability, band or airtime policy
ERR BUSY              TX queue busy (RF CONFIG deferred, or TXQUEUE=0 before drain)
ERR RADIO_NOT_READY   the command needs a ready radio and the radio is unavailable
ERR HARDWARE          RadioLib/CONFIG hardware failure
```

`ERR HARDWARE` accompanies a fail-closed transition: after a RadioLib failure
mid-apply the radio goes to `RADIO=FAILED` rather than reporting `READY` with a
suspect configuration.

`ERR BUSY` covers two cases. Radio-touching `SET` commands — `MODE=` or any
non-`GETRSSI` parameter — are rejected while queued TX jobs are pending or one is
executing, because a queued job transmits with the configuration current at
execution time; the client retries once the queue drains. `SET TXQUEUE=0` is
rejected on the same condition. `GET` queries and the runtime setters always
pass.

## Apply order and rejection rules

A `SET` with radio parameters is prevalidated as a whole, then applied
sequentially. It is not a transaction.

1. Parse. A line that is not `SET`/`SET …` answers `ERR UNKNOWN`; `SET` with no
   parameters answers `ERR MALFORMED`.
2. Validate the complete command — syntax, value ranges, chip-family capability,
   band frequency policy, duplicate keys — before any hardware is touched. An
   invalid command changes neither `MODE`, nor `GETRSSI`, nor any radio
   parameter.
3. Check the merged worst-case single-packet airtime of the effective
   configuration against `CONFIG_POLICY_MAX_AIRTIME_MS` (`20000.0` ms) and reject
   the whole command with `ERR INVALID` if it would be exceeded. See
   [limits](limits.md).
4. Apply `MODE=` first, before every mode-specific parameter.
5. Apply `GETRSSI=`. It is handled in the apply layer as a flag store and never
   reaches a driver setter; it is also the only key that does not count as
   radio-touching.
6. Apply the remaining parameters in order, skipping the keys that do not belong
   to the active mode.

There is no rollback: a RadioLib failure part-way through step 6 leaves the
earlier parameters applied, answers `ERR HARDWARE`, and marks the radio
`FAILED`.

`MODE=LORA` calls RadioLib `begin()` and lands on the band's boot RF defaults —
frequency and modulation parameters, never chip register defaults. `MODE=FSK`
calls `beginFSK()` and takes only the frequency from the band; the remaining FSK
parameters come from the family baseline in
[Chip-family differences](#chip-family-differences). Explicit parameters in the
same command line are applied on top of the mode switch; anything not set stays
at whatever the mode switch established.

After a mode reinitialisation the packet-received callback is restored and RX is
restarted.

## Unsolicited broadcasts

These lines are pushed to every connected CONF client of the band, independent of
any command.

| Message | Meaning |
|---|---|
| `TX=1\n` | A local transmit started |
| `TX=0\n` | A local transmit finished |
| `CAD=1\n` | Live RSSI at or above the `CADRSSI` threshold, only while `CADMONITOR=1` |
| `CAD=0\n` | Channel confirmed free again |
| `RSSI=-87.50\n` | Live RSSI while `GETRSSI=1`, two decimals |

`TX=1`/`TX=0` are emitted by the main loop, not by the TX path itself: a
transmit only bumps an atomic generation counter, and the monitoring tick
observes the counter and broadcasts the transitions. Async TX workers never
access client slots.

Each CAD transition is broadcast exactly once, on the edge the tick returns.
`CAD=0/1` monitoring is opt-in per band and off by default.

The RSSI stream stops automatically once no CONF client remains connected, so a
reconnecting client must send `SET GETRSSI=1` again.

## Examples

433 MHz LoRa/APRS-style setup. These are the 433 boot defaults except for
`POWER`, which the band boots at `10`:

```bash
echo "SET MODE=LORA FREQ=433.900 SF=12 BW=125 CR=5 CRC=1 PREAMBLE=8 SYNC=0x12 LDRO=1 POWER=17" \
  | socat - UNIX-CONNECT:/run/loraham/loraconf433.sock
```

868 MHz LoRa setup. This line reproduces the 868 boot defaults exactly:

```bash
echo "SET MODE=LORA FREQ=869.525 SF=11 BW=250 CR=5 CRC=1 PREAMBLE=16 SYNC=0x2B LDRO=AUTO POWER=10" \
  | socat - UNIX-CONNECT:/run/loraham/loraconf868.sock
```

433 MHz FSK setup. `RXBW=12.5` is on the SX127x raster; on an SX1262 board use a
value from the SX126x raster, for example `RXBW=11.7`:

```bash
echo "SET MODE=FSK FREQ=433.775 BR=4.8 FREQDEV=5.0 RXBW=12.5 POWER=10" \
  | socat - UNIX-CONNECT:/run/loraham/loraconf433.sock
```

433 MHz OOK setup, SX127x only:

```bash
echo "SET MODE=FSK FREQ=433.920 BR=1.2 RXBW=6.3 OOK=1 POWER=10" \
  | socat - UNIX-CONNECT:/run/loraham/loraconf433.sock
```

RSSI stream:

```bash
echo "SET GETRSSI=1" | socat - UNIX-CONNECT:/run/loraham/loraconf433.sock
echo "SET GETRSSI=0" | socat - UNIX-CONNECT:/run/loraham/loraconf433.sock
```

Queries:

```bash
printf 'GET STATUS\n' | socat - UNIX-CONNECT:/run/loraham/loraconf433.sock
printf 'GET STATS\n'  | socat - UNIX-CONNECT:/run/loraham/loraconf433.sock
printf 'GET CHANNEL\n' | socat - UNIX-CONNECT:/run/loraham/loraconf868.sock
```
