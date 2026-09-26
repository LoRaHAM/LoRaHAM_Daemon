# Examples

Enough to get a client of your own talking to the daemon. Nothing here is a framework — these are
the shortest programs that do something real, meant to be pasted, run and changed. The full command
set is in [conf-protocol.md](conf-protocol.md) and the frame layout in
[data-protocol.md](data-protocol.md).

The daemon must be running. Everything below assumes 433; swap `433` for `868` for the other band.

## Contents

- [Find the sockets](#find-the-sockets)
- [Ask the daemon how it is](#ask-the-daemon-how-it-is)
- [Send a packet with one line of shell](#send-a-packet-with-one-line-of-shell)
- [Receive packets in Python](#receive-packets-in-python)
- [Send a packet and read the result](#send-a-packet-and-read-the-result)
- [Receive packets in C](#receive-packets-in-c)
- [Receive packets in JavaScript](#receive-packets-in-javascript)
- [Where to go next](#where-to-go-next)

## Find the sockets

Under systemd they are in `/run/loraham`; started by hand with `LORAHAM_SOCKET_DIR=/tmp` they are in
`/tmp`. Every bundled client tries `/run/loraham` first and falls back, which is two lines in any
language:

```python
import os, stat
path = "/run/loraham/lora433f.sock"
if not (os.path.exists(path) and stat.S_ISSOCK(os.stat(path).st_mode)):
    path = "/tmp/lora433f.sock"
```

A path that exists but is not a socket must not win — that is the test the bundled clients make.

Three sockets per band: `lora433.sock` is a raw byte stream, `lora433f.sock` is the same traffic in
frames with RSSI and SNR, `loraconf433.sock` takes text commands.

## Ask the daemon how it is

The CONF socket speaks one command per line and answers one line. `socat` is the quickest way in:

```sh
printf 'GET STATUS\n'  | socat - UNIX-CONNECT:/run/loraham/loraconf433.sock
printf 'GET CHANNEL\n' | socat - UNIX-CONNECT:/run/loraham/loraconf433.sock
```

```text
STATUS RADIO=READY TX=0 CAD=0 GETRSSI=0 TXRESULT=0 TXMODE=MANAGED TXQUEUE=1 TXQ=0 TXQDROP=0
TXQREJECT=0 TXQSTALE=0 TXQRESULTDROP=0 TXQDONE=0 TXQLAST=NONE TXQSEQ=0 CADWAIT=1500 CADIDLE=250
CADPOLL=50 CADTXAFTERTIMEOUT=0 CADMONITOR=0 CADRSSI=-90 RXREADY=1

CHANNEL RADIO=READY BUSY=0 CAD=1 CADSCAN=1 CADSTATE=FREE RSSI=-164.00 PACKETRSSI=-164.00
LIVERSSI=-81.00 MODE=LORA TXMODE=MANAGED
```

(One line each; wrapped here to fit.) That capture is from a board on the `uputronics-ce0` preset.
`CADSCAN=1` because the SX127x driver polls the latched CAD bits in `RegIrqFlags` and needs no
DIO1, which this board does not route. Note the pairing worth understanding: `LIVERSSI=-81.00` is a
noisy channel — above the default `CADRSSI=-90` busy threshold — yet `CADSTATE=FREE`, because the
scan found no LoRa preamble. An RSSI threshold alone would have called that channel busy.

`GET CHANNEL` is the interesting one to poll while you move an antenna around — `LIVERSSI` follows
the noise floor.

Setting something works the same way and answers `OK`:

```sh
printf 'SET POWER=2\n' | socat - UNIX-CONNECT:/run/loraham/loraconf433.sock
```

## Send a packet with one line of shell

Bytes written to the raw DATA socket are transmitted:

```sh
printf 'N0CALL>APRS:hello from socat' | socat - UNIX-CONNECT:/run/loraham/lora433.sock
```

In the demonstrated run this short one-liner arrived in one `read()` and went out as one packet.
That is not a protocol guarantee: the socket is a byte stream, so write boundaries are not packet
boundaries. See [data-protocol.md](data-protocol.md).

Check it happened with `GET STATUS`: `TXQDONE` increments and `TXQLAST` says how it went.

Reading works the same way round — this prints received bytes as they arrive:

```sh
socat -u UNIX-CONNECT:/run/loraham/lora433.sock -
```

The raw socket has no packet boundaries and no metadata. For those, use the framed socket.

## Receive packets in Python

Every frame is a 3-byte header — type, then a little-endian length — followed by that many bytes.
An `RX_PACKET` payload starts with RSSI and SNR in hundredths, then the radio bytes.

```python
#!/usr/bin/env python3
import socket, struct, os, stat

path = "/run/loraham/lora433f.sock"
if not (os.path.exists(path) and stat.S_ISSOCK(os.stat(path).st_mode)):
    path = "/tmp/lora433f.sock"

s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(path)

def read_exactly(n):
    buf = b""
    while len(buf) < n:
        chunk = s.recv(n - len(buf))
        if not chunk:
            raise SystemExit("daemon closed the socket")
        buf += chunk
    return buf

while True:
    frame_type, length = struct.unpack("<BH", read_exactly(3))
    payload = read_exactly(length)
    if frame_type == 0x01:                                  # RX_PACKET
        rssi, snr = struct.unpack("<hh", payload[:4])
        print(f"RSSI {rssi/100:6.1f} dBm  SNR {snr/100:5.1f} dB  {payload[4:]!r}")
```

One line per packet:

```text
RSSI  -81.5 dBm  SNR   9.2 dB  b'N0CALL>APRS:hello from the air'
```

`read_exactly` is the only part that needs care: a stream socket will hand you half a frame if you
let it.

## Send a packet and read the result

Transmitting is the same framing with type `0x02`. The daemon reports what happened in a
`TX_RESULT` frame, but only if you ask for them first, once, on the CONF socket:

```sh
printf 'SET TXRESULT=1\n' | socat - UNIX-CONNECT:/run/loraham/loraconf433.sock
```

```python
payload = b"N0CALL>APRS:hello from a script"
s.sendall(struct.pack("<BH", 0x02, len(payload)) + payload)     # TX_PACKET

STATUS = {0: "OK", 1: "BUSY", 2: "CHANNEL_BUSY", 3: "RADIO_NOT_READY",
          4: "RADIO_ERROR", 5: "INVALID_PACKET", 6: "INVALID_BAND"}

frame_type, length = struct.unpack("<BH", read_exactly(3))
payload = read_exactly(length)
if frame_type == 0x04:                                          # TX_RESULT
    status, flags, seq = struct.unpack("<BBH", payload)
    print(f"TX_RESULT seq={seq} status={STATUS.get(status, status)} flags=0x{flags:02x}")
```

```text
TX_RESULT seq=1 status=OK flags=0x01
```

`flags=0x01` means the transmission went through the managed path, which waits for a clear channel.
`CHANNEL_BUSY` instead of `OK` means it waited and gave up — that is the daemon protecting the band,
not an error in your program. The other statuses and flags are in
[data-protocol.md](data-protocol.md).

## Receive packets in C

The same thing without a struct library, and it is still short:

```c
/* cc -Wall -o rx rx.c */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>

static int read_exactly(int fd, void *buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t k = read(fd, (char *)buf + got, n - got);
        if (k <= 0) return -1;
        got += (size_t)k;
    }
    return 0;
}

int main(void) {
    struct stat st;
    const char *path = (stat("/run/loraham/lora433f.sock", &st) == 0 && S_ISSOCK(st.st_mode))
                     ? "/run/loraham/lora433f.sock" : "/tmp/lora433f.sock";

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    strncpy(a.sun_path, path, sizeof(a.sun_path) - 1);
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) < 0) { perror("connect"); return 1; }

    for (;;) {
        unsigned char hdr[3], payload[259];
        if (read_exactly(fd, hdr, 3) < 0) break;
        unsigned len = hdr[1] | (hdr[2] << 8);
        if (read_exactly(fd, payload, len) < 0) break;
        if (hdr[0] != 0x01) continue;                       /* RX_PACKET only */
        short rssi = (short)(payload[0] | (payload[1] << 8));
        short snr  = (short)(payload[2] | (payload[3] << 8));
        printf("RSSI %6.1f dBm  SNR %5.1f dB  %.*s\n",
               rssi / 100.0, snr / 100.0, (int)len - 4, payload + 4);
        fflush(stdout);
    }
    return 0;
}
```

## Receive packets in JavaScript

Node opens a UNIX socket like any other stream. A browser cannot — a page has no raw socket API —
so a web front end needs a small local process like this one to bridge for it.

```js
// node rx.mjs
import net from "node:net";
import fs from "node:fs";

const run = "/run/loraham/lora433f.sock";
const path = fs.statSync(run, { throwIfNoEntry: false })?.isSocket() ? run : "/tmp/lora433f.sock";

const STATUS = ["OK", "BUSY", "CHANNEL_BUSY", "RADIO_NOT_READY",
                "RADIO_ERROR", "INVALID_PACKET", "INVALID_BAND"];

const sock = net.createConnection({ path });
let buf = Buffer.alloc(0);

sock.on("data", (chunk) => {
  buf = Buffer.concat([buf, chunk]);

  while (buf.length >= 3) {                          // the header is 3 bytes
    const type = buf[0];
    const len = buf.readUInt16LE(1);
    if (buf.length < 3 + len) break;                 // the rest of the frame is still coming

    const payload = buf.subarray(3, 3 + len);
    buf = buf.subarray(3 + len);                     // this frame is consumed

    if (type === 0x01) {                             // RX_PACKET
      const rssi = payload.readInt16LE(0) / 100;
      const snr = payload.readInt16LE(2) / 100;
      console.log(`RSSI ${rssi} dBm  SNR ${snr} dB  ${payload.subarray(4)}`);
    }

    if (type === 0x04) {                             // TX_RESULT
      console.log(`TX_RESULT seq=${payload.readUInt16LE(2)} ` +
                  `status=${STATUS[payload[0]]} ` +
                  `flags=0x${payload[1].toString(16).padStart(2, "0")}`);
    }
  }
});
```

Data arrives in whatever pieces the kernel hands over, so the buffer is kept across events and the
loop runs until what is left is no longer a complete frame. That is the same `read_exactly` problem
as in the other two examples, solved the other way round because the callback cannot block.

To transmit, append this to the same file. It uses the `sock` above, and the reply lands in the
handler you already have — that is what the `0x04` branch is for:

```js
sock.on("connect", () => {
  const body = Buffer.from("N0CALL>APRS:hello from node");
  const header = Buffer.alloc(3);
  header.writeUInt8(0x02, 0);                        // TX_PACKET
  header.writeUInt16LE(body.length, 1);
  sock.write(Buffer.concat([header, body]));
});
```

With `SET TXRESULT=1` already sent on the CONF socket:

```text
TX_RESULT seq=8 status=OK flags=0x01
```

Node is not on the Raspberry Pi image; install it if you want this one.

## Where to go next

- Poll `GET CHANNEL` in a loop and plot `LIVERSSI` — a two-line spectrum monitor.
- Set `SET GETRSSI=1` and read the stream the CONF socket then pushes at about 10 Hz.
- Open the framed socket and the CONF socket at once: transmit, and watch `TX=1` and `TX=0` arrive
  on CONF around your own transmission.
- Read [`../clients/`](../clients/) — the chat TUI and the iGate are the same three sockets, with
  more on top.

Two things worth knowing before you experiment on the air. Every transmission is real radio: keep
the power low and the antenna sensible while testing, and use a callsign you are entitled to —
`N0CALL` throughout this page is a placeholder, not a licence. And one process owns one radio, so
your client shares the daemon with whatever else is connected; it never gets the chip to itself.
