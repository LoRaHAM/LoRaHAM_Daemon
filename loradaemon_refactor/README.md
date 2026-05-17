# LoRaHAM_Daemon

`loraham_daemon` is the local hardware daemon for LoRaHAM_Pi / LoRaHAM Cartridge on Raspberry Pi. It initializes the 433 MHz and 868 MHz radio modules and exposes them to user programs through local UNIX sockets.

The daemon is the interface between the LoRaHAM radio hardware and applications such as LoRaHAM iGate, PiGate, LoRaHAM Chat, test tools, or custom clients.

## Purpose

- Control the dual-band LoRaHAM hardware from user space.
- Provide DATA sockets for raw packet TX/RX.
- Provide CONF sockets for runtime radio configuration.
- Keep radio access centralized so client programs do not need direct SPI/RadioLib access.
- Print live RX/TX/debug information when run in the foreground.

## Current architecture

| Area | File/module | Role |
|---|---|---|
| Main daemon | `loradaemon_320_108.cpp` | Radio setup, main loop, TX/RX flow, CAD/RSSI polling |
| Event loop | `event_loop.cpp`, `event_loop_epoll.cpp` | epoll-based socket waiting |
| UNIX sockets | `unix_socket.cpp` | Create and remove socket files |
| Client handling | `client_set.cpp` | Client slots, reads, closes, broadcasts |
| DATA TX | `data_tx.cpp` | Split DATA socket writes into RF chunks |
| CONFIG parser/apply | `config_parser.cpp`, `config_apply.cpp`, `config_dispatch.h` | Parse `SET KEY=VALUE` commands and apply RadioLib settings |
| Radio channel state | `radio_channel.cpp` | Per-band socket/client state, RSSI auto-stop, live RSSI |
| Timing/lifecycle | `daemon_timing.cpp`, `daemon_lifecycle.cpp` | RSSI timing and signal-based shutdown |

## Daemon command line

| Option | Default | Accepted values | Meaning |
|---|---:|---|---|
| none | foreground | - | Run in terminal and print traffic/debug output |
| `-d` | off | `-d` | Run in background; stdout/stderr go to `/tmp/lora_daemon.log` |

## UNIX socket interface

| Socket | Type | Direction | Payload | Meaning |
|---|---|---|---|---|
| `/tmp/lora433.sock` | DATA 433 | client ↔ daemon | raw bytes | TX raw bytes on 433 MHz; receive RF packets from 433 MHz |
| `/tmp/lora868.sock` | DATA 868 | client ↔ daemon | raw bytes | TX raw bytes on 868 MHz; receive RF packets from 868 MHz |
| `/tmp/loraconf433.sock` | CONF 433 | client ↔ daemon | text commands | Configure 433 MHz radio; receive CAD/RSSI text messages |
| `/tmp/loraconf868.sock` | CONF 868 | client ↔ daemon | text commands | Configure 868 MHz radio; receive CAD/RSSI text messages |

## Public limits and timing

| Item | Default/current value | Meaning |
|---|---:|---|
| `MAX_CLIENTS` | `10` | Client slots per socket group |
| `buf_SIZE` | `256` bytes | Internal RX/CONFIG buffer size |
| DATA TX chunk | `255` bytes | Maximum RF chunk generated from DATA socket input |
| Event-loop timeout | `10000 µs` / `10 ms` | Main loop socket wait timeout |
| RSSI interval | `100 ms` | `GETRSSI=1` stream cadence, about 10 Hz |
| CAD poll interval | `30` loop ticks | CAD polling cadence constant |

## DATA sockets

DATA sockets are raw byte streams. They have no line protocol and no length prefix.

| Action | Behavior |
|---|---|
| Client writes bytes to DATA socket | Daemon transmits them on the matching band |
| Write is larger than 255 bytes | Daemon splits it into multiple RF chunks |
| RF packet is received | Daemon broadcasts raw bytes to connected DATA clients of that band |
| Client disconnects | Daemon closes the slot and can reuse it |

Examples:

```bash
printf 'Hello LoRaHAM\n' | socat - UNIX-CONNECT:/tmp/lora433.sock
```

```bash
echo "FFFFFFFF67452301FFBFC1E80700000028505C33DC22F5051C" \
  | perl -pe 's/([0-9A-Fa-f]{2})/chr(hex($1))/ge' \
  | socat - UNIX-CONNECT:/tmp/lora868.sock
```

## CONF sockets

CONF sockets accept text commands:

```text
SET KEY=VALUE KEY=VALUE ...
```

Important behavior:

- A write should contain one complete `SET ...` command.
- Keys are parsed case-insensitively.
- `MODE=` is applied before other radio parameters.
- `GETRSSI=` is handled directly.
- LoRa-only keys are ignored in FSK mode.
- FSK-only keys are ignored in LoRa mode.
- Status/errors are printed to the daemon log; there is no stable OK/ERR response protocol on the CONF socket.
- `MODE=LORA` calls RadioLib `begin()`.
- `MODE=FSK` calls RadioLib `beginFSK()`.
- After mode reinitialization, the packet-received callback is restored and RX is restarted.

## Startup defaults

| Band | Mode | FREQ | SF | BW | CR | CRC | PREAMBLE | SYNC | LDRO | POWER |
|---|---|---:|---:|---:|---:|---:|---:|---|---|---:|
| 433 | LORA | `433.900` | `12` | `125` | `5` | `1` | `8` | `0x12` | `1` | `10` |
| 868 | LORA | `869.525` | `11` | `250` | `5` | `1` | `16` | `0x2B` | `AUTO` | `10` |

## CONFIG parameters

| Key | Mode | Default | Accepted values | Common values | Meaning |
|---|---|---|---|---|---|
| `MODE` | global | `LORA` | `LORA`, `FSK` | `LORA`, `FSK` | Select RadioLib mode |
| `FREQ` | LORA/FSK | 433: `433.900`, 868: `869.525` | number > 0, MHz | `433.775`, `433.900`, `869.525` | RF frequency |
| `POWER` | LORA/FSK | `10` | `0` to `20` dBm | `10`, `17`, `20` | TX power |
| `GETRSSI` | global | `0` | `0`, `1` | `1` start, `0` stop | Stream live RSSI to CONF clients |
| `SF` | LORA | 433: `12`, 868: `11` | `7` to `12` | `12`, `11`, `10`, `7` | LoRa spreading factor |
| `BW` | LORA | 433: `125`, 868: `250` | `7.8`, `10.4`, `15.6`, `20.8`, `31.25`, `41.7`, `62.5`, `125`, `250`, `500` kHz | `125`, `250` | LoRa bandwidth |
| `CR` | LORA | `5` | `5` to `8` | `5` | LoRa coding rate (`4/5` to `4/8`) |
| `CRC` | LORA | `1` | `0`, `1` | `1` | LoRa CRC off/on |
| `PREAMBLE` | LORA/FSK | 433: `8`, 868: `16` | LoRa: `6` to `65535`; FSK: `>=0` | `8`, `16`, `32` | Preamble length |
| `SYNC` | LORA/FSK | 433: `0x12`, 868: `0x2B` | LoRa: 1 byte; FSK: 1 or 2 bytes | `0x12`, `0x2B`, `0x2DD4` | Sync word |
| `LDRO` | LORA | 433: `1`, 868: `AUTO` | `AUTO`, `0`, `1` | `AUTO`, `1` for SF12/BW125 | Low Data Rate Optimization |
| `BR` | FSK | RadioLib/default unset by daemon | number > 0, kbps | `1.2`, `2.4`, `4.8`, `9.6`, `100` | FSK bitrate |
| `FREQDEV` | FSK | RadioLib/default unset by daemon | number > 0, kHz | `2.2`, `3.0`, `5.0`, `10.0` | Frequency deviation |
| `RXBW` | FSK | RadioLib/default unset by daemon | number > 0, kHz | `6.3`, `12.5`, `20.8`, `25.0`, `250.0` | RX filter bandwidth |
| `OOK` | FSK | RadioLib/default | `0`, `1` | `0` FSK, `1` OOK | Enable OOK mode |
| `SHAPING` | FSK | RadioLib/default | RadioLib-valid float | `0.0`, `0.3`, `0.5`, `1.0` | Data shaping / Gaussian filter |
| `ENCODING` | FSK | RadioLib/default | `0`, `1`, `2` | `0` NRZ, `1` Manchester, `2` Whitening | FSK encoding |

## CONF output messages

| Message | Socket | Meaning |
|---|---|---|
| `CAD=1\n` | matching CONF socket | LoRa channel activity detected |
| `CAD=0\n` | matching CONF socket | LoRa channel no longer active |
| `RSSI=-87.50\n` | matching CONF socket | Live RSSI while `GETRSSI=1` is active |
| log: `kein Client mehr verbunden -> GETRSSI auto-stop` | daemon stdout/log | RSSI stream stopped because no CONF client is connected |

`GETRSSI=1` automatically stops when no CONF client remains connected. A reconnect must send `SET GETRSSI=1` again.

## Examples

433 MHz LoRa/APRS-style setup:

```bash
echo "SET MODE=LORA FREQ=433.900 SF=12 BW=125 CR=5 CRC=1 PREAMBLE=8 SYNC=0x12 LDRO=1 POWER=17" \
  | socat - UNIX-CONNECT:/tmp/loraconf433.sock
```

868 MHz LoRa setup:

```bash
echo "SET MODE=LORA FREQ=869.525 SF=11 BW=250 CR=5 CRC=1 PREAMBLE=16 SYNC=0x2B LDRO=AUTO POWER=10" \
  | socat - UNIX-CONNECT:/tmp/loraconf868.sock
```

433 MHz FSK setup:

```bash
echo "SET MODE=FSK FREQ=433.775 BR=4.8 FREQDEV=5.0 RXBW=12.5 POWER=10" \
  | socat - UNIX-CONNECT:/tmp/loraconf433.sock
```

433 MHz OOK setup:

```bash
echo "SET MODE=FSK FREQ=433.920 BR=1.2 RXBW=6.3 OOK=1 POWER=10" \
  | socat - UNIX-CONNECT:/tmp/loraconf433.sock
```

RSSI stream:

```bash
echo "SET GETRSSI=1" | socat - UNIX-CONNECT:/tmp/loraconf433.sock
echo "SET GETRSSI=0" | socat - UNIX-CONNECT:/tmp/loraconf433.sock
```

## Warnings

This software is experimental and used at your own risk. Use only for amateur radio or laboratory operation. Frequencies, power levels, and modes must match your license and local regulations.

## Credits and license

Copyright (c) 2020-2026 Alexander Walter / LoRaHAM.
modified by Johannes Loose / 410733@gmail.com

The original project is licensed under GNU GPL v3 with additional conditions stated by the author:

- private/hobby use is free
- commercial use requires written permission / commercial license
- modifications should be reported to the author, preferably via pull request
- binaries may only be redistributed with the full source code
- no warranty; use at your own risk

