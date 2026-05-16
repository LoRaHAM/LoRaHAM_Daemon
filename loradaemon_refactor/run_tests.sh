#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
TEST_BIN="$SCRIPT_DIR/test_loradaemon_interface"
DAEMON_BIN="$SCRIPT_DIR/loraham_daemon"

tx_tests=false
rx_seconds="${RX_SECONDS:-15}"

usage() {
  cat <<EOF_HELP
Usage: ./loradaemon_tests/run_tests.sh [--TX] [--rx-seconds N]

Builds the daemon and interface test, then runs the test with its own daemon.

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

cmd=("$TEST_BIN" --bin "$DAEMON_BIN")

if [[ "$tx_tests" == true ]]; then
  cmd+=(--rf-tx --rx-seconds "$rx_seconds")
fi

exec "${cmd[@]}"
