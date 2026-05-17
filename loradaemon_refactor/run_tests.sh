#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
TEST_DIR="$SCRIPT_DIR/tests"
DAEMON_BIN="$SCRIPT_DIR/loraham_daemon"

tx_tests=false
rx_seconds="${RX_SECONDS:-15}"

usage() {
  cat <<EOF_HELP
Usage: ./loradaemon_refactor/run_tests.sh [--TX] [--rx-seconds N]

Builds the daemon and test binaries, then runs each test with its own daemon.

Options:
  --TX             Run RF transmit tests too
  --rx-seconds N   RX observation time for --TX, default: 15
  -h, --help       Show this help
EOF_HELP
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --TX)
      tx_tests=true
      shift
      ;;
    --rx-seconds)
      if [[ $# -lt 2 ]]; then
        echo "ERROR: --rx-seconds needs a value." >&2
        exit 2
      fi
      rx_seconds="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "ERROR: unknown option: $1" >&2
      echo >&2
      usage >&2
      exit 2
      ;;
  esac
done

if pgrep -x loraham_daemon >/dev/null; then
  echo "ERROR: loraham_daemon is already running:"
  pgrep -af loraham_daemon
  echo
  echo "Stop it first, then run this script again."
  exit 1
fi

"$SCRIPT_DIR/build.sh"
"$TEST_DIR/check_no_legacy_blocking_broadcasts.sh"

tests=(
  "$TEST_DIR/test_data_tx"
  "$TEST_DIR/test_client_output_queue"
  "$TEST_DIR/test_client_slot"
  "$TEST_DIR/check_client_slot_structure.sh"
  "$TEST_DIR/test_client_nonblocking"
  "$TEST_DIR/test_client_queued_broadcast"
  "$TEST_DIR/test_client_slow_output"
  "$TEST_DIR/test_event_loop_output_flush"
  "$TEST_DIR/test_tx_result"
  "$TEST_DIR/test_radio_health"
  "$TEST_DIR/test_radio_controller_skeleton"
  "$TEST_DIR/test_radio_controller_accessors"
  "$TEST_DIR/check_tx_result_propagation.sh"
  "$TEST_DIR/check_radio_health_guards.sh"
  "$TEST_DIR/check_radio_controller_cad_rssi.sh"
  "$TEST_DIR/check_radio_controller_rx_flow.sh"
  "$TEST_DIR/check_radio_controller_tx_flow.sh"
  "$TEST_DIR/check_no_legacy_radio_globals.sh"
  "$TEST_DIR/check_radio_controller_shutdown.sh"
  "$TEST_DIR/check_radio_controller_unique_ptr.sh"
  "$TEST_DIR/check_radio_controller_led.sh"
  "$TEST_DIR/check_debug_cli_logging.sh"
  "$TEST_DIR/check_startup_lifecycle_logging.sh"
  "$TEST_DIR/check_context_logging_prefixes.sh"
  "$TEST_DIR/check_radio_init_shutdown_logging.sh"
  "$TEST_DIR/check_socket_client_logging.sh"
  "$TEST_DIR/check_config_context_logging.sh"
  "$TEST_DIR/check_data_tx_context_logging.sh"
  "$TEST_DIR/check_rx_cad_rssi_context_logging.sh"
  "$TEST_DIR/test_rf_packet"
  "$TEST_DIR/test_event_loop"
  "$TEST_DIR/test_daemon_timing"
  "$TEST_DIR/test_daemon_lifecycle"
  "$TEST_DIR/test_unix_socket"
  "$TEST_DIR/test_config_parser"
  "$TEST_DIR/test_config_stream_buffer"
  "$TEST_DIR/test_config_value"
  "$TEST_DIR/test_config_policy"
  "$TEST_DIR/test_config_validate"
  "$TEST_DIR/test_config_apply_transactional"
  "$TEST_DIR/check_readme_config_policy.sh"
  "$TEST_DIR/check_config_apply_strict_parsing.sh"
  "$TEST_DIR/check_transactional_config_apply.sh"
  "$TEST_DIR/test_config_dispatch"
  "$TEST_DIR/test_interface_baseline"
  "$TEST_DIR/test_config_stream"
  "$TEST_DIR/test_rssi_multiclient"
  "$TEST_DIR/test_client_lifecycle"
  "$TEST_DIR/test_known_issues"
)

overall_rc=0
results=()

record_test_ok() {
  local test_name="$1"

  results+=("OK   $test_name")
}

record_test_fail() {
  local test_name="$1"
  local reason="$2"

  results+=("FAIL $test_name $reason")
  overall_rc=1
}

cleanup_lingering_daemon() {
  local test_bin="$1"
  local test_name="$2"

  if pgrep -x loraham_daemon >/dev/null; then
    echo "ERROR: loraham_daemon is still running after $test_bin"
    pgrep -af loraham_daemon || true
    pkill -TERM -x loraham_daemon 2>/dev/null || true
    sleep 1
    pkill -KILL -x loraham_daemon 2>/dev/null || true
    record_test_fail "$test_name" "daemon-still-running"
  fi
}

run_one_test() {
  local test_bin="$1"
  local test_name
  local rc
  local cmd

  test_name="$(basename "$test_bin")"

  echo
  echo "================================================================"
  echo "Running: $test_bin"
  echo "================================================================"

  cmd=("$test_bin" --bin "$DAEMON_BIN")

  if [[ "$test_bin" == "$TEST_DIR/test_interface_baseline" && "$tx_tests" == true ]]; then
    cmd+=(--rf-tx --rx-seconds "$rx_seconds")
  fi

  if "${cmd[@]}"; then
    record_test_ok "$test_name"
  else
    rc=$?
    record_test_fail "$test_name" "rc=$rc"
  fi

  sleep 1
  cleanup_lingering_daemon "$test_bin" "$test_name"
}

for test_bin in "${tests[@]}"; do
  run_one_test "$test_bin"
done

echo
echo "================================================================"
echo "Final test summary"
echo "================================================================"
for result in "${results[@]}"; do
  echo "$result"
done

if [[ "$overall_rc" -eq 0 ]]; then
  echo "OVERALL OK"
else
  echo "OVERALL FAIL"
fi

exit "$overall_rc"
