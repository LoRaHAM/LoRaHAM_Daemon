# LoRaHAM daemon tests

This directory contains the daemon test sources.

## Usage

Run the regular test suite from the daemon source directory:

```bash
./run_tests.sh
```

Or from this `tests/` directory:

```bash
../run_tests.sh
```

The runner builds the production daemon with `build.sh`, builds the test
binaries, and then runs each test against the built daemon binary.

RF transmit tests are disabled by default. Enable them only with suitable
hardware, frequency settings, and RF conditions:

```bash
./run_tests.sh --TX --rx-seconds 15
```

## Runner behavior

`run_tests.sh` currently runs 25 test binaries. It refuses to start if a
`loraham_daemon` process is already running, checks for lingering daemon
processes after each test, parses per-test `Summary:` lines, and prints a final
OK/FAIL/SKIP/XFAIL/XPASS table.

## Test concept

The tests should protect externally relevant behavior:

- daemon build, startup, shutdown, and signal handling
- public command-line behavior, including selected-radio mode
- public UNIX socket behavior and cleanup
- CONFIG command parsing, validation, rejection, and apply safety
- DATA socket chunking and RF packet handling helpers
- client lifecycle, queued output, and slow/nonblocking client behavior
- timing, event-loop, radio-health, and TX-result semantics that affect daemon behavior


## Coverage overview

CONFIG/protocol:

- `test_config_parser`
- `test_config_stream_buffer`
- `test_config_stream`
- `test_config_value`
- `test_config_policy`
- `test_config_validate`
- `test_config_apply_transactional`
- `test_config_dispatch`

DATA/RF/TX:

- `test_data_tx`
- `test_tx_result`
- `test_rf_packet`

Client/socket/runtime:

- `test_client_output_queue`
- `test_client_nonblocking`
- `test_client_queued_broadcast`
- `test_client_slow_output`
- `test_event_loop_output_flush`
- `test_client_lifecycle`
- `test_rssi_multiclient`
- `test_unix_socket`

Lifecycle/helper behavior:

- `test_daemon_radio_selection`
- `test_event_loop`
- `test_daemon_timing`
- `test_daemon_lifecycle`
- `test_radio_health`

Public integration baseline:

- `test_interface_baseline`

