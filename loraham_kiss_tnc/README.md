# LoRaHAM KISS TNC Bridge

KISS/TCP TNC bridge for `LoRaHAM_Daemon`.

It exposes a KISS/TCP port for APRS clients and talks to the LoRaHAM daemon through its Unix sockets.

```text
APRS client <-> KISS/TCP <-> loraham_kiss_tnc <-> /tmp/lora433.sock <-> loraham_daemon
```




## Build

```bash
./loraham_kiss_tnc/build.sh
```

## Test

```bash
./loraham_kiss_tnc/run_tests.sh
```

## Run

```bash
/tmp/loraham_kiss_tnc --help
```

## Usage

```bash
Usage: /tmp/loraham_kiss_tnc [OPTIONS]

LoRaHAM KISS/TCP TNC bridge

Options:
  -c, --config FILE        Load config file
      --kiss-host HOST     KISS/TCP bind host
      --kiss-port PORT     KISS/TCP bind port
      --data-socket PATH   LoRaHAM data socket
      --conf-socket PATH   LoRaHAM config socket
      --rx-freq MHz        RX/config frequency
      --tx-freq MHz        TX/config frequency
      --rx-only            Disable TX
      --tx                 Enable TX
  -v, --verbose            Verbose output
  -h, --help               Show help

```