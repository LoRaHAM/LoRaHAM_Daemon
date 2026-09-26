# Hardware

Which boards the daemon drives, how one is selected with `--hw`, the wiring and GPIO lines each
preset claims, which pairs of boards can share one Pi, and the host prerequisites (packages,
RadioLib, SPI). Building and running the daemon is in
[`../loraham_daemon/README.md`](../loraham_daemon/README.md); the full flag list is in
[CLI](cli.md); systemd and multi-instance deployment are in [Deployment](deployment.md).

## Contents

* [Host and packages](#host-and-packages)
* [RadioLib](#radiolib)
* [Raspberry Pi SPI interface](#raspberry-pi-spi-interface)
* [Board presets](#board-presets)
* [Wiring and claimed pins](#wiring-and-claimed-pins)
* [Capability flags](#capability-flags)
* [Chip-family differences](#chip-family-differences)
* [Two processes on one Pi](#two-processes-on-one-pi)
* [Adding a board](#adding-a-board)

## Host and packages

Target platform: Raspberry Pi 3/4/5 running a Raspbian image.

    sudo apt update
    sudo apt install g++ make cmake build-essential -y
    sudo apt install liblgpio-dev -y
    sudo apt install libncurses5-dev libncursesw5-dev -y
    sudo apt install socat -y

`libncurses` is needed only for the chat client, `socat` only for talking to the sockets by hand.

## RadioLib

    git clone https://github.com/makrohard/LoRaHAM_Daemon ~/LoRaHAM

    git clone https://github.com/jgromes/RadioLib ~/RadioLib
    git -C ~/RadioLib checkout 187ef24791c3d844939b2be13a68bd890bd04e4c

RadioLib is used from its source checkout by the daemon build script — no `make install` required.
The checkout above pins the exact RadioLib commit the test suite and CI run against, recorded in
[`../.github/ci/radiolib.lock`](../.github/ci/radiolib.lock). Building against an arbitrary newer
RadioLib may compile but is untested.

## Raspberry Pi SPI interface

    sudo raspi-config nonint set_config_var dtparam=spi on /boot/firmware/config.txt # Enable SPI

    # Add dtoverlay=spi0-0cs after dtparam=spi=on if it is not already there
    if ! sudo grep -q '^\s*dtoverlay=spi0-0cs' /boot/firmware/config.txt; then
        sudo sed -i '/^\s*dtparam=spi=on/a dtoverlay=spi0-0cs' /boot/firmware/config.txt
    fi

Both take effect on the next boot.

Every preset drives CS as a plain GPIO (manual-CS operation), so the kernel must not hold the
hardware CE lines: `dtparam=spi=on` together with `dtoverlay=spi0-0cs` releases the kernel
hardware-CE claim on BCM 8/7. This is host configuration, applied once — the daemon never reads or
writes `config.txt` and cannot check it.

All radios use SPI channel 0, `/dev/spidev0.0`. Two daemon processes on one Pi share that bus and
serialise it per transaction through a process-shared SPI lock.

With packages, RadioLib and SPI in place, build and run the daemon as described in
[`../loraham_daemon/README.md`](../loraham_daemon/README.md).

## Board presets

`--hw PRESET` selects the hardware profile for this process's radio. The default is `loraham`. The
known presets are exactly `loraham, uputronics-ce0, uputronics-ce1, waveshare-sx1262`.

| Preset | Chip family | Board | Band |
|---|---|---|---|
| `loraham` (default) | SX127x | LoRaHAM_Pi dual-module wiring | per band: `433` drives an SX1278, `868` an RFM95 |
| `uputronics-ce0` | SX127x | Uputronics Pi Zero LoRa expansion board (HopeRF RFM95/98W), CE switch on CE0 | either band |
| `uputronics-ce1` | SX127x | same board, CE switch on CE1 | either band |
| `waveshare-sx1262` | SX1262 | Waveshare SX1262 LoRaWAN Node HAT | either band; LF and HF variants are pin-identical |

* The band stays the identity key. `--radio` is resolved first and the profile is then resolved for
  that band, so a band-independent preset takes its frequency band from `--radio`, not from the
  board entry.
* `loraham` serves both bands from one preset, one daemon process per band.
* `uputronics-ce0` and `uputronics-ce1` are the two CE-switch variants of one board type; two such
  boards can be stacked. CE0/CE1 names the chip-select slot, not a band.
* Board-variant frequency ranges (the Uputronics 434 or 868 variant, the Waveshare LF or HF
  variant) are vendor properties the daemon never sees. What the code enforces is the per-band
  frequency policy — `430.0`–`440.0` MHz on the 433 process, `863.0`–`870.0` MHz on the 868
  process (`config_policy_freq_valid_band`); see [Limits](limits.md).
* Every preset is driven as a raw LoRa radio. There is no LoRaWAN stack; "LoRaWAN" appears only in
  a board name.
* An unknown preset is rejected once the band is frozen: the daemon prints the offending name and
  the known list, then the usage text, and exits with a usage error.

### What `POWER=20` drives, per preset

`POWER` is the chip's setting, not a measurement at the antenna connector, and the profile knows
wiring and chip family, not the output stage. With `--high-power` ([cli.md](cli.md)) an SX127x
process accepts exactly `POWER=20`; what that does depends on the board:

| Preset | Output stage | `POWER=20` means |
|---|---|---|
| `uputronics-ce0` / `-ce1` | bare HopeRF RFM95/98W: the chip's PA_BOOST drives the antenna | the datasheet's own +20 dBm (100 mW) case, under its restrictions (duty ≤ 1 %, VSWR ≤ 3:1, VDD 2.4–3.7 V — [limits.md](limits.md)). The proof case for the feature; requested chip power, not a calibrated connector measurement |
| `waveshare-sx1262` | SX1262, single PA path | nothing new: `0`–`20` was always accepted and the flag is inert |
| `loraham`, 868 | described as a 100 mW module; exact module unconfirmed | as a bare module; do not read a confirmed BOM into it |
| `loraham`, 433 | **HopeRF RFM98PW**: an enhanced-power module based on the RF98/RF96 device, nominally +30 dBm (1 W), 5–6 V supply, hundreds of mA in TX; its internal RF chain is not modelled by the daemon and the die is not independently established | the chip drives the module's RF chain 3 dB harder. **Unvalidated on the LoRaHAM 433 RFM98PW: the reviewed module documentation does not establish the permitted chip-drive setting or the resulting module output.** Compression, supply and matching can change the outcome; +3 dB of drive does not guarantee +3 dB of output or that the amplifier tolerates it. The permission is the same switch there by the maintainer's decision, and this note is the only guard |

Chip OCP protects the chip's PA; it does not limit an external amplifier's supply current.

## Wiring and claimed pins

Pin semantics are family-dependent, following the RadioLib `Module(hal, cs, irq, rst, gpio)`
argument order:

| Family | `irq` | `gpio` |
|---|---|---|
| SX127x | DIO0 | DIO1 |
| SX1262 | DIO1 | BUSY |

A pin value below zero means "not connected" and is passed to RadioLib as `RADIOLIB_NC`.

| Preset | CS | IRQ | RST | `gpio` | LED | Claimed BCM lines |
|---|---:|---:|---:|---:|---:|---|
| `loraham` (433) | 8 | 25 | 5 | 24 | 13 | 8, 25, 5, 24, 13 |
| `loraham` (868) | 7 | 16 | 6 | 12 | 19 | 7, 16, 6, 12, 19 |
| `uputronics-ce0` | 8 | 25 | — | — | 6 | 8, 25, 6 |
| `uputronics-ce1` | 7 | 16 | — | — | 13 | 7, 16, 13 |
| `waveshare-sx1262` | 21 | 16 | 18 | 20 | — | 21, 16, 18, 20, 6 |

* `loraham` is bit-identical to the pre-profile hardcoded wiring: `Module(hal,8,25,5,24)` on 433
  and `Module(hal,7,16,6,12)` on 868.
* On the Uputronics profiles DIO5 is routed but unused — BCM 24 on CE0, BCM 12 on CE1. It is held
  in the profile's `aux` field for documentation only and is not part of the claimed set.
* The two Uputronics board LEDs (BCM 6 "LAN", BCM 13 "INTERNET") are shared bus-style across
  stacked boards. Assignment is slot-based, CE0 to BCM 6 and CE1 to BCM 13, so two processes never
  claim the same line; both physical boards display both indicators, which is inherent to the board
  design. Polarity on BCM 6/13 is active-high: `daemon_led_set_pin()` writes the level with no
  inversion, so "on" is a high level.
* The LED line comes from the resolved profile (`daemon_hw_profile.led_pin`), not from the band.
  `DAEMON_LED_PIN_433` (`13`) and `DAEMON_LED_PIN_868` (`19`) are the defaults of the `loraham`
  preset only; with `--hw uputronics-ce0` the LED is BCM 6 and with `uputronics-ce1` it is BCM 13.
  A profile with no LED line disables the feature cleanly rather than failing.
* The Waveshare HAT has no board LED, because BCM 6 is its antenna-switch line. The board silk
  reads "TXEN", but the line must be LOW during TX and HIGH during RX, so it is registered in
  RadioLib's `rxEn` slot: `setRfSwitchPins((uint32_t)txen_pin, RADIOLIB_NC)`. TX routing runs
  through DIO2 (`setDio2AsRfSwitch(true)`, checked and fail-closed on error). The TCXO is fed from
  DIO3; the voltage (`1.8` V) comes from the profile and is applied inside `begin()`, and again in
  `beginFSK()`.
* The claimed-pin list is not authored by hand. `profile_claimed_finish()` derives it from `cs`,
  `irq`, `rst`, `gpio`, `txen`, `rxen` and `led_pin`, skipping negatives, capped at
  `DAEMON_HW_MAX_CLAIMED` (`8`).

## Capability flags

| Flag | `loraham` | `uputronics-ce0/-ce1` | `waveshare-sx1262` |
|---|---|---|---|
| `cad_scan_available` | `true` | `true` | `true` |
| `fsk_stream_available` | `true` | `false` | `false` |
| `reset_wired` | `true` | `false` | `true` |
| `tcxo_voltage` | `0.0` | `0.0` | `1.8` |

* `cad_scan_available` is the only flag the runtime acts on, and it means **"this profile and
  driver combination has trustworthy active CAD"** — deliberately the combination and not the chip
  family, since a future board could carry a CAD-capable chip on wiring that does not support it.

  It is `true` on **all three** presets, Uputronics included. That is a change: it used to be
  `false` there because the blocking SX127x `scanChannel()` waits on DIO0 and polls DIO1 for the
  detection, and those boards do not route DIO1, so every scan reported false-FREE. The SX127x
  driver now polls the latched `CadDone`/`CadDetected` bits in `RegIrqFlags` instead, so the verdict
  comes off the chip rather than off the wiring and needs no DIO1. Uputronics therefore reports
  `CADSCAN=1` and runs real listen-before-talk.

  Measured on the air when the flag was flipped: on a 433 channel whose noise floor forced the old
  RSSI fallback to report `BUSY` at rest (`LIVERSSI ≈ −81 dBm` against a −90 dBm `CADRSSI`), the
  same box at the same noise level now reports `CADSTATE=FREE` — and reports `BUSY` for 25 of 25
  polls while another station actually occupies the channel.

  `CADRSSI` still exists and still matters, but for the **passive** mechanism — the `CAD=0/1`
  monitor and the degraded path a profile would take if it ever declared no trustworthy CAD. It is
  no longer the Uputronics substitute for listen-before-talk.
* `fsk_stream_available` is documentation only. It is set by the preset table and read nowhere in
  the daemon outside the unit tests, so it gates nothing: `SET MODE=FSK` is accepted on the
  Uputronics and Waveshare profiles exactly as on any other. The underlying board fact still holds
  — SX127x FSK stream modes need DIO1, which these boards do not route — but nothing enforces it
  the way scan-based CAD is enforced.
* `reset_wired` is `false` where RESET is not routed. A daemon restart is then a warm start against
  whatever state the chip is in; the condition is logged once at init.

  Observed on an `uputronics-ce1` 868 instance, 2026-09-16: after repeated restarts the receiver
  went deaf while the daemon reported `RADIO=READY CADSCAN=1 RXREADY=1`. It transmitted normally
  (`outcome=ok`), heard nothing at all — `LIVERSSI` flat and `CADSTATE=FREE` throughout a burst that
  the other band reported `BUSY` for — and the stale state showed up as a wrong noise floor
  (`RSSI=-86.00` where the band normally reads `-157.00`). RadioLib writes a register only when its
  cache differs, so `begin()` alone need not correct a chip that survived the restart in an
  unexpected state.

  A power cycle is the documented recovery, but a **modem round trip recovers it without one**:

  ```
  SET MODE=FSK
  SET MODE=LORA
  ```

  That runs `beginFSK()` and then the full `begin()` path, rewriting far more registers than a
  repeated `begin()`. Reception resumed on the next frame and the reported floor returned to
  `-157.00`.

  The general point for operators: `RADIO=READY` means a configured radio, a usable IRQ path and an
  armed receiver. It does **not** mean the antenna path works, and nothing in the daemon can assert
  that — only a decode from a second station can.
* `tcxo_voltage` above zero means `begin()` must set the DIO3 TCXO voltage.

  Where it is zero the board runs on a plain crystal rather than a temperature-compensated
  oscillator, so two such boards can sit further apart in frequency than a narrowband FSK receiver
  likes. Expect FSK to be less forgiving than LoRa between them: chirp demodulation tolerates far
  more frequency error than an FSK discriminator does. If an FSK link is poor where LoRa is fine,
  `SET RXBW=<khz>` on the receiving side is the control to reach for.

  One diagnostic that is worth more than the RSSI: **`RX=0` together with `RXDROPS=0` means no
  completed frame reached the daemon's counted read-and-validate path**, whereas frames that arrive
  and fail CRC show up as `RXDROPS`. The first points at configuration or frequency, the second at
  signal quality. It is a strong hint, not a measurement of the front end — an RX indication with a
  non-positive length is re-armed without recording a drop, so "nothing was demodulated at all" is
  more than the counters can prove.
* Live RSSI on SX1262 comes from the SX126x instantaneous-RSSI command, never from SX127x register
  addresses.
* LoRa sync word: RadioLib maps the SX127x byte (`0x12` / `0x2B`) onto SX1262 via the compatibility
  control bits.
* FSK receive bandwidth is validated against the active chip family's raster, and the two rasters
  barely intersect. `12.5` and `6.3` kHz are on the SX127x raster only; an SX126x board rejects
  them and uses its own values (`4.8`, `5.8`, `7.3`, `9.7`, `11.7`, `14.6`, `19.5`, `23.4`, and so
  on). Any FSK example quoting an SX127x bandwidth is implicitly SX127x-only.
* OOK is SX127x-only. On an SX126x chip every `OOK` key is rejected at prevalidation, including
  `OOK=0`, because the chip has no OOK modulator.

On-air behaviour that only a bench can establish, recorded in
[`../loraham_daemon/HW-ONAIR-CHECKLIST.md`](../loraham_daemon/HW-ONAIR-CHECKLIST.md) rather than in
code: compatibility with SX127x and SX126x counterparts for the LoRa-APRS, MeshCom and Meshtastic
parameter sets; `0x2B` (the MeshCom raster) in both directions against an SX1262 station; SX1262 to
SX127x cross-family decode in the RX direction; and the Waveshare LF/433 variant as fully
on-air-validated with the HF/868 binding software-supported but on-air-untested. The last of these
is an argument by analogy, and the code supports the premise: one pin set serves both variants and
the driver is band-agnostic.

## Chip-family differences

| File | Contents |
|---|---|
| `hardware_profile.cpp`, `hardware_profile.h` | the `--hw` preset table: wiring, chip family, capabilities, LED, claimed pins |
| `sx127x_driver.cpp`, `sx127x_driver.h` | the SX1278/RFM9x driver; all SX127x register constants live there, including the `begin()`-failure diagnosis |
| `sx1262_driver.cpp`, `sx1262_driver.h` | the SX126x driver: TCXO via DIO3, DIO2-as-RF-switch plus the inverse antenna-switch line, SX126x CRC/sync/power semantics, instantaneous-RSSI live RSSI |
| `daemon_led.cpp`, `daemon_led.h` | Raspberry Pi GPIO LED setup and per-radio LED pin state; the LED is a per-band hardware and activity resource, not the instance-ownership lock |

## Two processes on one Pi

One process drives one radio, so dual-band operation is two processes. A pair works when the two
claimed sets are disjoint.

| Combination | Verdict |
|---|---|
| `loraham` 433 + `loraham` 868 | supported — `8, 25, 5, 24, 13` against `7, 16, 6, 12, 19`, disjoint |
| `uputronics-ce0` + `uputronics-ce1` | supported — `8, 25, 6` against `7, 16, 13`, disjoint |
| `waveshare-sx1262` + `uputronics-ce0` | not supported — BCM 6 is both the Waveshare antenna-switch line and the CE0 LED line |
| `waveshare-sx1262` + `uputronics-ce1` | not supported — BCM 16 is Waveshare DIO1 and Uputronics DIO0 |
| `waveshare-sx1262` + `waveshare-sx1262` | not supported — the preset is band-independent, so both processes resolve the identical claimed set and every pin collides |

`waveshare-sx1262` and `uputronics-ce1` overlap on BCM 16 and nothing else. BCM 6 is a physical LED
of the Uputronics hardware but is not in the `uputronics-ce1` claimed set, whose LED is BCM 13, so
it plays no part in that conflict. Any pair not listed follows the same rule: intersecting claimed
sets fail closed, disjoint ones do not.

Conflicts are caught twice.

1. By the daemon, before any GPIO access — including the status-LED claim. Each process takes one
   advisory lock per pin it will drive, `gpio<N>.lock` in the trusted runtime lock directory, in
   ascending pin order. A pin already held fails the boot closed with
   `[GPIO] Fehler: GPIO 6 bereits von einem anderen Prozess beansprucht` and the process exits with
   `LORAHAM_EXIT_LOCK_ERROR` (`4`), which is not restartable.
2. By the kernel, since GPIO line claims are exclusive. A failed claim produces a profile-aware
   diagnosis line naming the BCM pin, the band and the function that pin has in this profile.

There is no LED-override mechanism. Reassigning the CE0 LED to free BCM 6 for a Waveshare HAT was
considered and dropped for lack of a use case; `daemon_led_configure()` is only ever called with
`daemon_hw_profile.led_pin`, and no LED option exists on the command line.

The same diagnosis path covers a missing board. Without the HAT present, `begin()` fails with
`CHIP_NOT_FOUND`, exactly one profile-aware line is printed — the profile name and its
`CS`/`DIO1`/`RST`/`BUSY`/`TXEN` values for SX1262, `CS`/`DIO0`/`RST`/`DIO1`/`LED` for SX127x — and
the daemon exits fail-closed.

Two stacked Uputronics boards are two daemon processes, sharing SPI through the per-transaction
lock and separated by disjoint GPIO claims. For the unit files, socket paths and per-unit overrides
that go with a two-process setup, see [Deployment](deployment.md).

## Adding a board

For a board of a chip family the daemon already supports:

1. Add one preset row to `profile_fill()` in `hardware_profile.cpp`: pins, capability flags,
   `led_pin`, then call `profile_claimed_finish()`. Do not write out the claimed-pin list — it is
   derived.
2. Extend the preset string in `daemon_hardware_profile_known()`. There is no second list to
   update: `--help` and the invalid-preset error message both render that same string.
3. Document the board on this page.

For a new chip family, a driver implementation is required as well. The `RadioDriver` interface in
`radio_driver.h` is shipped and already has two implementations. Implement its pure-virtual
members — `begin()`, `switchMode()`, `applyLoraParam()`, `applyFskParam()`, `readLiveRssi()`,
`rssiProbe()`, `chipName()` and `chipFamily()` — then dispatch on the profile family in
`hw_driver_create()` and `hw_diagnose_begin_failure()` in `daemon_radio_init.cpp`. The daemon
runtime itself stays untouched; see [Architecture](architecture.md) for where those pieces sit.
