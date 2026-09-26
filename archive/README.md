# Archived LoRaHAM daemons

This directory holds the single-file LoRaHAM daemons. They remain available and usable, and
everything needed to build and run them is on this page.

| File | Notes |
|---|---|
| [`loradaemon_320_106.cpp`](loradaemon_320_106.cpp) | single-file daemon, both bands in one process |
| [`loradaemon_320_108a.cpp`](loradaemon_320_108a.cpp) | the later of the two; adds the FSK/OOK and RSSI-streaming examples documented in its header, and broadcasts `TX=1` / `TX=0` to connected CONF clients around each transmission |

The archived sources are preserved as they are upstream. Use this page for the current tested build
commands; the historical comments inside the files are kept unchanged, and one of them in
`loradaemon_320_108a.cpp` still names a much older source file in its long compile line.

Those headers are still worth reading: each carries the full set of `SET` examples (LoRa, FSK, OOK,
RSSI streaming) and the LDRO-per-bandwidth table, which this page only samples.

## How they behave

- **One process serves both bands.** There is no `--radio` flag; 433 and 868 are handled together.
- **Sockets live in `/tmp`**, hardcoded:

      /tmp/lora433.sock       DATA 433
      /tmp/lora868.sock       DATA 868
      /tmp/loraconf433.sock   CONF 433
      /tmp/loraconf868.sock   CONF 868

  The bundled clients find them there through their own `/tmp` fallback.
- **One option:** `-d` runs in the background. Without it you see all traffic on the terminal.
- **No systemd units.** These predate `/run/loraham` and the template units, so they are started
  by hand or from whatever supervisor you prefer.
- **They will not run with the LHPC stacks.** LoRaHAM_Pi Control drives the current daemon and
  nothing else — see below.

The wiring is fixed in the source — the original LoRaHAM_Pi dual-module pinout, BCM numbering:

| Band | CS | DIO0 | RST | DIO1 | LED |
|---|---|---|---|---|---|
| 433 | 8 | 25 | 5 | 24 | 13 |
| 868 | 7 | 16 | 6 | 12 | 19 |

## Get the source

    git clone https://github.com/makrohard/LoRaHAM_Daemon ~/LoRaHAM
    cd ~/LoRaHAM/archive

Every command on this page is run from that directory.

## Prerequisites on the Raspberry Pi image

    sudo apt update
    sudo apt install g++ make cmake build-essential -y
    sudo apt install liblgpio-dev -y
    sudo apt install socat -y

## RadioLib

These sources include `<RadioLib.h>` and `hal/RPi/PiHal.h` from a RadioLib checkout:

    git clone https://github.com/jgromes/RadioLib ~/RadioLib

    cd ~/RadioLib
    mkdir build/
    cd build
    cmake ..
    make

`sudo make install` also works and is what the original instructions used, but it is not required:
the compile lines below name the checkout's include paths and its `build/libRadioLib.a` directly.

**Known-good revisions.** Both files were compile-checked with GCC 15.3.1 against
`187ef24791c3d844939b2be13a68bd890bd04e4c` (`7.7.1-57-g187ef2479`), which is the revision this
repository pins for the current daemon in [`../.github/ci/radiolib.lock`](../.github/ci/radiolib.lock),
and against `13b7c7cf84b191006da20f82bdb386f2efc96334` (`7.6.0-67-g13b7c7cf8`). Either works, so one
checkout serves both trees. Other revisions may work; these two have been checked.

## Raspberry Pi hardware interface

    sudo raspi-config nonint set_config_var dtparam=spi on /boot/firmware/config.txt # Enable SPI

    # Add dtoverlay=spi0-0cs after dtparam=spi=on if it is not already there
    if ! sudo grep -q '^\s*dtoverlay=spi0-0cs' /boot/firmware/config.txt; then
        sudo sed -i '/^\s*dtparam=spi=on/a dtoverlay=spi0-0cs' /boot/firmware/config.txt
    fi

Both take effect on the next boot.

## Compile

Short form, as it stands in each file's header (substitute `320_106` for the other one):

    g++ -o loraham_daemon loradaemon_320_108a.cpp \
        -I$HOME/RadioLib/src \
        -I$HOME/RadioLib/src/modules \
        -I$HOME/RadioLib/src/protocols/PhysicalLayer \
        $HOME/RadioLib/build/libRadioLib.a -llgpio

Long form, with an explicit standard and optimisation:

    g++ -std=c++11 -O2 -o loraham_daemon loradaemon_320_108a.cpp \
        -I$HOME/RadioLib/src \
        -I$HOME/RadioLib/src/hal \
        -I$HOME/RadioLib/src/modules \
        -I$HOME/RadioLib/src/protocols/PhysicalLayer \
        $HOME/RadioLib/build/libRadioLib.a \
        -llgpio -lpthread

Both forms were checked against the RadioLib revision named above, for both files. The original
instructions wrote these paths out as `/home/raspberry/RadioLib/...`; `$HOME` is the same thing on
a Pi whose user is not `raspberry`.

## Run

    ./loraham_daemon          # traffic on the terminal
    ./loraham_daemon -d       # background

Then a client — see [Clients](#clients) below.

## Configuring the radios

Send a text line to a CONF socket. `socat` is the simplest way from a terminal:

    # LoRa-APRS on 433
    echo "SET FREQ=433.900 SF=12 BW=125 CR=5 CRC=1 PREAMBLE=8 SYNC=0x12 LDRO=1 POWER=17" \
      | socat - UNIX-CONNECT:/tmp/loraconf433.sock

    # Meshtastic-style 868
    echo "SET FREQ=869.525 SF=11 BW=250 CR=5 CRC=1 PREAMBLE=16 SYNC=0x2B LDRO=1 POWER=10" \
      | socat - UNIX-CONNECT:/tmp/loraconf868.sock

    # single parameter
    echo "SET FREQ=433.775" | socat - UNIX-CONNECT:/tmp/loraconf433.sock
    echo "SET POWER=5"      | socat - UNIX-CONNECT:/tmp/loraconf868.sock

FSK needs `MODE=FSK`, and `MODE=LORA` re-initialises the module back to LoRa:

    echo "SET MODE=FSK FREQ=433.775 BR=4.8 FREQDEV=5.0 RXBW=12.5 POWER=10" \
      | socat - UNIX-CONNECT:/tmp/loraconf433.sock
    echo "SET MODE=FSK FREQ=433.920 BR=1.2 FREQDEV=0.0 RXBW=6.3 OOK=1 POWER=10" \
      | socat - UNIX-CONNECT:/tmp/loraconf433.sock
    echo "SET MODE=LORA FREQ=433.900 SF=12 BW=125 CR=5 CRC=1 PREAMBLE=8 SYNC=0x12 LDRO=1 POWER=17" \
      | socat - UNIX-CONNECT:/tmp/loraconf433.sock

RSSI streaming runs at 10 Hz on the same CONF socket, in LoRa and in FSK, and also picks up
non-LoRa signals. Output lines look like `RSSI=-87.50`:

    echo "SET GETRSSI=1" | socat - UNIX-CONNECT:/tmp/loraconf433.sock
    echo "SET GETRSSI=0" | socat - UNIX-CONNECT:/tmp/loraconf433.sock

It stops by itself once no CONF client is connected, and needs `GETRSSI=1` again after a reconnect.

Sending data is a write to a DATA socket:

    echo -n "$(printf '%02X' 8)Der Text" \
      | perl -pe 's/^([0-9A-Fa-f]{2})|./$1 ? chr(hex($1)) : $&/ge' \
      | socat - UNIX-CONNECT:/tmp/lora433.sock

More examples, including the LDRO-per-bandwidth table and further FSK bit rates, are in each
file's header comment.

## Clients

A daemon on its own does nothing visible. The chat TUI, the APRS iGate, the dual-band RSSI bars and
the Meshtastic decryptor are in [`../clients/`](../clients/), and all of them fall back to the
`/tmp` sockets these daemons use. [`../clients/README.md`](../clients/README.md) has the compile
line and the options for each. Chat additionally needs:

    sudo apt install libncurses5-dev libncursesw5-dev -y

## The other daemon in this repository

[`../loraham_daemon/`](../loraham_daemon/) is the current tree. It takes a different approach: one
process per band selected with `--radio`, instance and GPIO locks, CAD/LBT, a framed DATA protocol
with RX metadata and TX results, sockets under `/run/loraham` with systemd units, and a test suite.
Its documentation is in [`../loraham_daemon/README.md`](../loraham_daemon/README.md).

Both are in this repository and both can be built from it.

**The LHPC stacks need that tree, not this one.** LoRaHAM_Pi Control launches one
`--radio <band>` process per band and talks to `/run/loraham`, including the framed DATA sockets
and the CONF reply protocol. The daemons here have none of that: one process for both bands, no
`--radio`, sockets in `/tmp`, no framed sockets. So every LHPC stack — daemon, chat, voice, KISS
TNC, Graywolf, Meshtastic, MeshCom, MeshCore, Reticulum — requires `../loraham_daemon/`. Run the
daemons on this page standalone with the clients in [`../clients/`](../clients/).

## Licence

    Copyright (c) 2020-2026 Alexander Walter
    Maintained by Alexander Walter
    Licensed under the GNU GPL v3 (see ../LICENSE)

The licence header is also at the top of each source file.
