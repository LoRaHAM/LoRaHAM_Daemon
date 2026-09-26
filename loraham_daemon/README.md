# LoRaHAM_Daemon

`loraham_daemon` is a device driver for SPI-attached LoRa radios. It was written for the
LoRaHAM_Pi HAT and the LoRaHAM Cartridge on a Raspberry Pi, which is what it is tested on, but it
drives any SX127x or SX126x wired directly to the SPI bus. It reaches the chip through RadioLib and
exposes it to applications on local UNIX sockets: raw DATA streams, packet-boundary-preserving
framed DATA, and a CONF socket for runtime radio configuration.

Each process owns exactly one radio, selected with the mandatory `--radio 433` or `--radio 868`.
Dual-band operation runs two processes. Keeping radio access in one place is the point: iGate,
PiGate, chat, the MeshCom bridge and any custom client integrate through the sockets instead of
touching SPI or RadioLib themselves.

This page is what the daemon is and how to get it running. Everything else has its own page — see
[Documentation](#documentation).

## Contents

* [Build](#build)
* [Test](#test)
* [Run](#run)
* [Documentation](#documentation)

## Build

Prerequisites are a C++ compiler, the lgpio development headers, a RadioLib source and build tree,
and pthread support — [`../docs/hardware.md`](../docs/hardware.md) has the package list and the
pinned RadioLib checkout.

    ./build.sh

| Option | Meaning |
|---|---|
| `--output PATH` | Output binary path; default `./loraham_daemon` next to `build.sh` |
| `--radiolib-dir DIR` | RadioLib source tree containing `src/` and `build/libRadioLib.a` |
| `--debug` | Build with `-O0 -g` instead of the release defaults |
| `--strict` | Build with `-Werror` |
| `--clean` | Remove the daemon binary and exit |

## Test

    ./run_tests.sh

Builds the daemon and the test binaries, runs the non-RF suite, then removes the generated test
binaries.

| Option | Meaning |
|---|---|
| `--strict` | Build with `-Werror` |
| `--sanitizer MODE` | Test-only build: `asan-ubsan` or `tsan` |
| `--TX` | Also run the RF transmit smoke tests |
| `--rx-seconds N` | RX observation time for `--TX`; default `15` |
| `--timeout-seconds N` | Timeout per test; default `60` |

`--TX` keys a real transmitter. It needs free radio hardware, SPI and GPIO access, and a safe RF
setup — plain `./run_tests.sh` is the normal check. What the suite covers, and which tests skip
without radio hardware, is in [`tests/README.md`](tests/README.md).

## Run

    LORAHAM_SOCKET_DIR=/tmp ./loraham_daemon --radio 433

That is the direct, no-systemd start: sockets land in `/tmp`, and the bundled clients find them
there through their own fallback. Run it in the foreground and it prints live RX, TX and debug
output.

`--rflog on --rflog-path /absolute/rf-daemon-433.log` keeps a persistent RF log: one line per
frame received (with RSSI/SNR) or transmitted (with its outcome), raw payload as hex and ASCII,
UTC timestamps. The file is capped at 5 MB — the tail moves to `<path>.1` by copy-truncate, so
the inode never changes and an external truncate is safe. The path must be absolute; `on`
without a path refuses to start. LoRaHAM Pi Control passes one file per band.

For the real deployment — systemd units, sockets under `/run/loraham`, the `loraham` system user
and the lock directories — see [`../docs/deployment.md`](../docs/deployment.md).

## Documentation

| | |
|---|---|
| [`../docs/hardware.md`](../docs/hardware.md) | Boards, `--hw` presets, wiring, SPI, running two processes on one Pi |
| [`../docs/deployment.md`](../docs/deployment.md) | systemd, sockets on disk, users and groups, locks, exit codes |
| [`../docs/cli.md`](../docs/cli.md) | Every command-line flag and the per-band startup defaults |
| [`../docs/conf-protocol.md`](../docs/conf-protocol.md) | CONF command set, parameters, queries and replies |
| [`../docs/data-protocol.md`](../docs/data-protocol.md) | Raw and framed DATA wire formats |
| [`../docs/limits.md`](../docs/limits.md) | Limits, timing, TX and CAD behaviour |
| [`../docs/architecture.md`](../docs/architecture.md) | Module map and runtime design |
| [`../docs/examples.md`](../docs/examples.md) | Short programs that talk to the daemon, in shell, Python and C |
| [`CHANGELOG.md`](CHANGELOG.md) | What changed between versions |
| [`../clients/README.md`](../clients/README.md) | The programs that talk to these sockets |
