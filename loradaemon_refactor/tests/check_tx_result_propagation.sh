#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REFACTOR_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
DAEMON="$REFACTOR_DIR/loradaemon_320_108.cpp"
DATA_TX="$REFACTOR_DIR/data_tx.cpp"

rc=0

echo "Checking TX result propagation..."

if grep -nE '\bvoid[[:space:]]+lora_send[[:space:]]*\(' "$DAEMON"; then
  echo "ERROR: lora_send still returns void." >&2
  rc=1
fi

if ! grep -qE '\bTxResult[[:space:]]+lora_send[[:space:]]*\(' "$DAEMON"; then
  echo "ERROR: lora_send does not return TxResult." >&2
  rc=1
fi

if ! grep -q 'tx_result_is_ok' "$DAEMON"; then
  echo "ERROR: send_data_chunk does not check TxResult." >&2
  rc=1
fi

if ! grep -q 'DATA-TX abgebrochen' "$DAEMON"; then
  echo "ERROR: send_data_chunk does not log TX abort reason." >&2
  rc=1
fi

if ! grep -q 'DATA-TX aborted after' "$DATA_TX"; then
  echo "ERROR: data_tx does not log aborted chunk processing." >&2
  rc=1
fi

exit "$rc"
