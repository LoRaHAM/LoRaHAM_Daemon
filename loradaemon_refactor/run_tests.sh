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

tests=(
  "$TEST_DIR/test_data_tx"
  "$TEST_DIR/test_event_loop"
  "$TEST_DIR/test_daemon_timing"
  "$TEST_DIR/test_config_parser"
  "$TEST_DIR/test_config_dispatch"
  "$TEST_DIR/test_interface_baseline"
  "$TEST_DIR/test_config_stream"
  "$TEST_DIR/test_rssi_multiclient"
  "$TEST_DIR/test_client_lifecycle"
  "$TEST_DIR/test_known_issues"
)

overall_rc=0
results=()

for test_bin in "${tests[@]}"; do
  echo
  echo "================================================================"
  echo "Running: $test_bin"
  echo "================================================================"

  cmd=("$test_bin" --bin "$DAEMON_BIN")

  if [[ "$test_bin" == "$TEST_DIR/test_interface_baseline" && "$tx_tests" == true ]]; then
    cmd+=(--rf-tx --rx-seconds "$rx_seconds")
  fi

  if "${cmd[@]}"; then
    results+=("OK   $(basename "$test_bin")")
  else
    rc=$?
    results+=("FAIL $(basename "$test_bin") rc=$rc")
    overall_rc=1
  fi

  sleep 1

  if pgrep -x loraham_daemon >/dev/null; then
    echo "ERROR: loraham_daemon is still running after $test_bin"
    pgrep -af loraham_daemon || true
    pkill -TERM -x loraham_daemon 2>/dev/null || true
    sleep 1
    pkill -KILL -x loraham_daemon 2>/dev/null || true
    results+=("FAIL $(basename "$test_bin") daemon-still-running")
    overall_rc=1
  fi
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
