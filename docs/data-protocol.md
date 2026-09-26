# DATA protocol

This page is the wire specification for the two payload interfaces of the daemon: the raw DATA sockets and the framed DATA sockets. It covers frame layout, field widths, byte order, size limits, RX metadata and `TX_RESULT`. It does not describe the CONF command set that switches these behaviours on — see [CONF protocol](conf-protocol.md) — nor the socket paths on disk, which are in [Deployment](deployment.md), nor the queue and CAD timings behind a TX result, which are in [Limits and timing](limits.md).

## Contents

- [Raw DATA sockets](#raw-data-sockets)
- [Framed DATA sockets](#framed-data-sockets)
- [Frame layout](#frame-layout)
- [Frame types](#frame-types)
- [`RX_PACKET` payload](#rx_packet-payload)
- [`TX_RESULT` payload](#tx_result-payload)
- [Size limits](#size-limits)
- [Framing rules and errors](#framing-rules-and-errors)
- [Deferred `TX_RESULT`](#deferred-tx_result)
- [Python examples](#python-examples)

## Raw DATA sockets

Raw DATA sockets are byte streams. They have no line protocol and no length prefix, and **a
client's `write()` calls do not define RF packet boundaries**: the daemon transmits whatever one
`read()` returns, split into packets of at most `255` bytes. Two quick writes can arrive together
and go out as one packet; one large write can arrive in pieces. Use the framed sockets when packet
boundaries matter.

| Action | Behaviour |
|---|---|
| Client writes bytes to a DATA socket | The daemon transmits them on the matching band |
| More than `255` bytes arrive at once | The daemon splits what it read into RF packets of at most `255` bytes |
| RF packet is received | The daemon broadcasts the raw bytes to all connected DATA clients of that band |

```bash
printf 'Hello LoRaHAM\n' | socat - UNIX-CONNECT:/run/loraham/lora433.sock
```

```bash
echo "FFFFFFFF67452301FFBFC1E80700000028505C33DC22F5051C" \
  | perl -pe 's/([0-9A-Fa-f]{2})/chr(hex($1))/ge' \
  | socat - UNIX-CONNECT:/run/loraham/lora868.sock
```

## Framed DATA sockets

Framed DATA sockets are opt-in binary stream sockets that preserve RF packet boundaries. They are a second interface, not a replacement: raw DATA clients continue to receive raw RF bytes exactly as before, independently of any framed client on the same band.

Raw DATA sockets carry payload only. Framed DATA sockets carry the payload **plus, on RX, the frame's RSSI and SNR** in the frame header (see the layout below). Status and config broadcasts (`TX=`, `CAD=`, `RSSI=`) go to CONF slots alone and are never written to a DATA or framed DATA socket.

## Frame layout

Every frame, in both directions, is a 3-byte header followed by the payload.

| Offset | Size | Meaning |
|---:|---:|---|
| `0` | 1 byte | frame type |
| `1` | 2 bytes | payload length, little-endian `uint16` |
| `3` | `length` bytes | payload |

## Frame types

| Type | Name | Direction | Meaning |
|---:|---|---|---|
| `0x01` | `RX_PACKET` | daemon to client | One complete received RF packet with RSSI/SNR metadata |
| `0x02` | `TX_PACKET` | client to daemon | One complete RF packet to transmit |
| `0x03` | `ERROR` | daemon to client | UTF-8 error text |
| `0x04` | `TX_RESULT` | daemon to client | One result per TX, emitted only when enabled with `SET TXRESULT=1` on the matching CONF socket |

## `RX_PACKET` payload

RX metadata is 4 bytes and is placed before the RF bytes.

| Payload offset | Size | Type | Meaning |
|---:|---:|---|---|
| `0` | 2 bytes | little-endian `int16` | RSSI in centi-dBm, or `-32768` when unavailable |
| `2` | 2 bytes | little-endian `int16` | SNR in centi-dB, or `-32768` when unavailable; FSK always reports unavailable |
| `4` | `rf_len` bytes | bytes | the received RF payload, at most `255` bytes |

The payload length of an `RX_PACKET` frame is therefore `4 + rf_len`.

## `TX_RESULT` payload

The `TX_RESULT` payload length is exactly `4` bytes.

| Payload offset | Size | Type | Meaning |
|---:|---:|---|---|
| `0` | 1 byte | `uint8` | status |
| `1` | 1 byte | bit mask | flags |
| `2` | 2 bytes | little-endian `uint16` | sequence number |

Status values:

| Value | Name |
|---:|---|
| `0` | `OK` |
| `1` | `BUSY` |
| `2` | `CHANNEL_BUSY` |
| `3` | `RADIO_NOT_READY` |
| `4` | `RADIO_ERROR` |
| `5` | `INVALID_PACKET` |
| `6` | `INVALID_BAND` |

Flag bits:

| Bit | Mask | Meaning |
|---:|---:|---|
| `0` | `0x01` | managed-mode attempt |
| `1` | `0x02` | reserved, currently never set |
| `2` | `0x04` | `MANAGED` send-after-CAD-timeout |

## Size limits

| Item | Value | Meaning |
|---|---:|---|
| RF payload | `255` bytes | Maximum accepted in a `TX_PACKET` and maximum carried inside an `RX_PACKET` |
| RX metadata | `4` bytes | `int16` RSSI in c-dBm followed by `int16` SNR in c-dB |
| Complete `RX_PACKET` frame | `262` bytes | 3-byte header plus 4 metadata bytes plus 255 RF bytes |
| `TX_RESULT` payload | `4` bytes | Exactly, always |

## Framing rules and errors

- A `TX_PACKET` payload must be at most `255` RF bytes and contains no metadata.
- One valid `TX_PACKET` maps to one RF transmit attempt. It is not split, unlike raw DATA stream input.
- One received RF packet is sent to framed clients as exactly one `RX_PACKET` frame.
- Oversized `TX_PACKET` frames and unsupported frame types are rejected with exactly one `ERROR` frame. The parser then re-syncs after the rejected frame's declared payload length, so a following valid frame on the same connection is parsed normally.

## Deferred `TX_RESULT`

When a framed TX is queued, the immediate success `TX_RESULT` is suppressed. The final result is delivered later, as a `TX_RESULT` frame addressed to the framed client slot that originated the transmission.

If that slot has closed or been reused before the completion arrives, the stale final `TX_RESULT` is dropped and counted in `TXQSTALE`. Because the deferred flag bit is never set, deferred delivery is recognisable by the suppressed immediate result rather than by a flag.

## Python examples

Reading one frame, and decoding RX metadata when it is an `RX_PACKET`:

```python
import socket
import struct

SIGNAL_UNAVAILABLE = -32768

def recv_exact(sock, n):
    data = bytearray()
    while len(data) < n:
        chunk = sock.recv(n - len(data))
        if not chunk:
            raise EOFError("socket closed")
        data += chunk
    return bytes(data)

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect("/run/loraham/lora868f.sock")

header = recv_exact(sock, 3)
frame_type, payload_len = struct.unpack("<BH", header)
payload = recv_exact(sock, payload_len)

if frame_type == 0x01:
    rssi_cdbm, snr_cdb = struct.unpack("<hh", payload[:4])
    rf_payload = payload[4:]
    rssi = None if rssi_cdbm == SIGNAL_UNAVAILABLE else rssi_cdbm / 100.0
    snr = None if snr_cdb == SIGNAL_UNAVAILABLE else snr_cdb / 100.0
    print(rssi, snr, rf_payload)
```

Sending one packet:

```python
import socket
import struct

payload = b"Hello LoRaHAM"
frame = struct.pack("<BH", 0x02, len(payload)) + payload

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect("/run/loraham/lora868f.sock")
sock.sendall(frame)
```
