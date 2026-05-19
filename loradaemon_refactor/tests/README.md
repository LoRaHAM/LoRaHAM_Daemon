# LoRaHAM daemon tests

This directory contains the daemon test sources.

## Usage

Run the full regular test suite from the daemon source directory:

```bash
./run_tests.sh
```

From this `tests/` directory, run:

```bash
../run_tests.sh
```

The test runner first builds the production daemon with `build.sh`, then builds
the test binaries, then runs each test against the built daemon binary.

RF transmit tests are disabled by default. Enable them only when suitable
hardware, frequency settings, and RF conditions are available:

```bash
./run_tests.sh --TX --rx-seconds 15
```

The test runner refuses to start if a `loraham_daemon` process is already
running. After each test it checks for lingering daemon processes and terminates
them if necessary.

## Test concept

The test suite is based on current behavior. 
It should answer these questions:

- Does the daemon build and start correctly?
- Do the public command-line and socket interfaces behave as expected?
- Does the CONFIG protocol parse, validate, reject, and apply commands safely?
- Do DATA, RF packet, and TX-result paths preserve the expected wire behavior?
- Do clients connect, disconnect, block, and receive queued data safely?
- Do timing, lifecycle, radio-health, and event-loop helper semantics remain
  stable enough for daemon behavior?

## Coverage overview

CONFIG/protocol coverage:

- `test_config_parser`
- `test_config_stream_buffer`
- `test_config_stream`
- `test_config_value`
- `test_config_policy`
- `test_config_validate`
- `test_config_apply_transactional`
- `test_config_dispatch`

DATA/RF/TX coverage:

- `test_data_tx`
- `test_tx_result`
- `test_rf_packet`

Client/socket coverage:

- `test_client_output_queue`
- `test_client_nonblocking`
- `test_client_queued_broadcast`
- `test_client_slow_output`
- `test_event_loop_output_flush`
- `test_client_lifecycle`
- `test_rssi_multiclient`
- `test_unix_socket`

Lifecycle/helper behavior coverage:

- `test_event_loop`
- `test_daemon_timing`
- `test_daemon_lifecycle`
- `test_radio_health`

Public integration baseline:

- `test_interface_baseline`