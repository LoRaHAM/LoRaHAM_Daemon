# LoRaHAM daemon refactor workspace

This directory contains a copied daemon source and ex-ante tests for the
planned refactor. The upstream daemon source is not modified directly.

Target platform: Raspberry Pi / Raspberry Pi OS with LoRaHAM_Pi or LoRaHAM
Cartridge hardware.

## Build and test

```bash
./loradaemon_refactor/run_tests.sh
```

Optional RF transmit smoke test:

```bash
./loradaemon_refactor/run_tests.sh --TX --rx-seconds 5
```

`--TX` sends real RF packets. Use it only on suitable test hardware and
frequencies.

## Test groups

- `test_interface_baseline`: public socket/config/RSSI behavior
- `test_config_stream`: persistent config sockets and RSSI stream behavior
- `test_client_lifecycle`: client connect/disconnect behavior
- `test_known_issues`: future behavior, currently XFAIL

## Refactor rule

Keep the public socket interface stable unless a change is intentional,
documented, and covered by tests.

Functional changes must be marked with a short comment near the changed code.

## Planned direction

1. Extract transport/client helpers.
2. Extract config parsing.
3. Introduce `RadioChannel` for 433/868 MHz.
4. Move timing into explicit timer state.
5. Introduce an event-loop abstraction.
6. Switch the event-loop implementation to `epoll`.
7. Convert XFAIL tests into normal tests one by one.
