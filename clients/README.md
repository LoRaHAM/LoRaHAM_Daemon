# Client programs

The programs that talk to the daemon over its UNIX sockets. Each is a single self-contained C file
with no shared headers, so each compiles on its own.

All of them look for the sockets under `/run/loraham` first and fall back to `/tmp`. The test is
whether the `/run` path exists *and is a socket*, so the same binary works under the systemd
deployment, next to a daemon started by hand with `LORAHAM_SOCKET_DIR=/tmp`, and with the
[archived single-file daemons](../archive/README.md), which only ever create `/tmp` sockets.

All five carry the same fallback helper, and each was run against both worlds to confirm it: with
only `/run/loraham` present each connected there, and with only `/tmp` present each connected
there instead.

| Program | Band | Needs | |
|---|---|---|---|
| chat | 433 | ncurses | [`chat/`](chat/README.md) |
| iGate | 433 | — | [`igate/`](igate/README.md) |
| `rssi_dualbar_101.c` | 433 + 868 | — | below |
| `mt_decrypt_parser_socket_200.c` | 868 | OpenSSL | below |

Filenames carry their version number. That is how these programs are published, and the version in
the name is the version of the program.

## Start a daemon first

These programs were written for the older single-file daemon, which served both bands from one
process, put its sockets in `/tmp`, and always transmitted immediately. The current daemon is one
process per band and needs to be told which band, which board and where to put its files:

```bash
mkdir -p ~/loraham-run && chmod 755 ~/loraham-run          # lock dir, not group-writable

LORAHAM_SOCKET_DIR=/tmp LORAHAM_RUNTIME_DIR=~/loraham-run \
  ./loraham_daemon --radio 433 --hw loraham --tx-mode direct
```

Four things differ from the old world, and each of them will stop a client working if missed:

| | |
|---|---|
| `--radio 433` | mandatory; one process per band. For 868 as well, run a second process with `--radio 868` and the **same** lock directory |
| `--hw loraham` | which board is fitted. `loraham` is the default; see [hardware.md](../docs/hardware.md) for the others |
| `LORAHAM_SOCKET_DIR=/tmp` | without it the sockets go to `/run/loraham`, which only the systemd deployment creates |
| `LORAHAM_RUNTIME_DIR` | the lock directory. Without it the daemon fails closed on `/run/lock/loraham`; with a group-writable one it refuses that too |

**`--tx-mode` is the one that is easy to miss**, because the old daemon had no such thing. The
default is `managed`, which waits for a clear channel and can defer or drop a packet — and none of
these programs asks for a transmit result, so a dropped packet is silent. `direct` transmits when
told, like the daemon they were written for. `managed` is the better citizen on a shared band and
is what LoRaHAM_Pi Control runs chat with; use it unless silent deferral matters to you.

Per program: chat and the iGate need 433; the Meshtastic decoder needs 868;
`rssi_dualbar` talks to both bands, so it needs both processes.

## Running chat and the iGate at the same time

They will fight over the radio. The iGate sets the frequency on every TX and RX, and chat does the
same — and chat transmits on the iGate's RX frequency while receiving on its TX frequency. Running
both means collisions. What does work is watching: chat shows every RF transmission the iGate
receives.

## rssi_dualbar_101.c

Live RSSI bars for both bands at once. It connects to the 433 and 868 CONF sockets, sets the
frequency, switches on `GETRSSI=1`, and draws two bars from -160 dBm to 0 dBm with a peak marker
holding the last five seconds. `Ctrl-C` sends `GETRSSI=0` to both daemons on the way out.

It needs **both** daemons, sharing one lock directory:

    LORAHAM_SOCKET_DIR=/tmp LORAHAM_RUNTIME_DIR=~/loraham-run ./loraham_daemon --radio 433 --hw loraham &
    LORAHAM_SOCKET_DIR=/tmp LORAHAM_RUNTIME_DIR=~/loraham-run ./loraham_daemon --radio 868 --hw loraham &

    gcc -O2 -Wall -o rssi_bar_dual clients/rssi_dualbar_101.c

    ./rssi_bar_dual                    # 433.175 / 869.525 MHz
    ./rssi_bar_dual 433.775 868.300

## mt_decrypt_parser_socket_200.c

A Meshtastic decryptor and parser that reads the **868** DATA socket and prints a table per
PortNum.

It reads only, so the transmit mode does not matter — but it needs the **868** daemon:

    LORAHAM_SOCKET_DIR=/tmp LORAHAM_RUNTIME_DIR=~/loraham-run ./loraham_daemon --radio 868 --hw loraham

    gcc -Wall -O2 -o mt_decrypt_socket clients/mt_decrypt_parser_socket_200.c -lcrypto

## Licence

    Copyright (c) 2020-2026 Alexander Walter
    Licensed under the GNU GPL v3 (see ../LICENSE)

The licence header is also at the top of each source file.
