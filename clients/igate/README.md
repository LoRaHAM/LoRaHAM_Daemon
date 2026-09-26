# LoRaHAM iGate

The APRS iGate on the 433 DATA and CONF sockets. Two versions are here, both buildable.

`loraham_iGate_106.c` is the later one — the LoRaHAM PiGate. Over `105d` it adds the correct IS
login handshake (verified / unverified), the correct RF→IS upload format (`qAR`/`qAO` with
RFONLY/NOGATE/TCPIP checks), digipeater path logic (WIDE1-1, WIDE2-n), 25-second duplicate
suppression, IS→RF only for stations actually heard within the last 30 minutes, automatic
reconnect for both the IS connection and the LoRa socket, and a standalone repeater mode that
needs no internet at all.

    gcc -Wall -o loraham_igate clients/igate/loraham_iGate_106.c

`loraham_iGate_105d.c` is the earlier version:

    gcc -Wall -o loraham_igate clients/igate/loraham_iGate_105d.c

## Options

`-h` prints the same list in German, which is what the program itself speaks.

    -c CALL       callsign
    -t TX_FREQ    TX frequency MHz
    -r RX_FREQ    RX frequency MHz
    -i SEK        IS beacon interval, seconds
    -f SEK        RF beacon interval, seconds
    -L LAT        beacon latitude    (e.g. 4827.72N)
    -O LON        beacon longitude   (e.g. 00957.94E)
    -x LAT_DEC    filter latitude    (decimal, e.g. 48.46)
    -y LON_DEC    filter longitude   (decimal, e.g. 9.96)
    -R KM         filter radius, km — only pass stations within it
    -S SYMBOL     APRS symbol, or table and symbol: `/&`, `\&`
    -D MODE       digipeater: 0=off, 1=WIDE1 (default), 2=WIDE1+WIDE2
    -m            relay IS->RF messages
    -p            repeater only (no internet, no IS)
    -d            run in the background

The filter coordinates are independent of the beacon ones, so the radius can be centred somewhere
other than your own position.

Example:

    ./loraham_igate -c DB0ABC-10 -t 433.900 -r 433.775 -L 4827.70N -O 00957.60E -f 600 -i 1200 -d

It finds the sockets under `/run/loraham` first and falls back to `/tmp`.

The iGate and chat cannot sensibly run at the same time — see
[the note in the client index](../README.md#running-chat-and-the-igate-at-the-same-time).

## Start a daemon first

The iGate needs a 433 daemon; it uses that band's DATA and CONF sockets.

```bash
mkdir -p ~/loraham-run && chmod 755 ~/loraham-run
LORAHAM_SOCKET_DIR=/tmp LORAHAM_RUNTIME_DIR=~/loraham-run \
  ./loraham_daemon --radio 433 --hw loraham --tx-mode direct
```

`--tx-mode direct` reproduces the send-when-told behaviour of the daemon this program was written
for. The default `managed` mode waits for a clear channel and may defer or drop a beacon, and the
iGate never asks for a transmit result, so it would not notice — [the trade-off is in the client
index](../README.md#start-a-daemon-first).

## Licence

    Copyright (c) 2020-2026 Alexander Walter
    Licensed under the GNU GPL v3 (see ../../LICENSE)

The licence header is also at the top of each source file.
