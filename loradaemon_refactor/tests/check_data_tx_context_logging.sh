#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REFACTOR_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
DAEMON="$REFACTOR_DIR/loradaemon_320_108.cpp"
DATA_TX_H="$REFACTOR_DIR/data_tx.h"
DATA_TX_CPP="$REFACTOR_DIR/data_tx.cpp"
TEST_DATA_TX="$REFACTOR_DIR/tests/test_data_tx.cpp"

rc=0

echo "Checking DATA-TX context debug logging..."

require() {
  local file="$1"
  local pattern="$2"
  local label="$3"

  if ! grep -Fq -- "$pattern" "$file"; then
    echo "ERROR: missing DATA-TX log pattern: $label" >&2
    rc=1
  fi
}

forbid() {
  local file="$1"
  local pattern="$2"
  local label="$3"

  if grep -Fq -- "$pattern" "$file"; then
    echo "ERROR: obsolete DATA-TX log pattern remains: $label" >&2
    rc=1
  fi
}

require "$DATA_TX_H" "typedef void (*DataTxLogFn)(void *ctx, const char *msg);" "DATA-TX log callback typedef"
require "$DATA_TX_H" "DataTxLog log" "DATA-TX process log parameter"
require "$DATA_TX_CPP" "data_tx_log_bytes(&log, n);" "DATA-TX bytes trace"
require "$DATA_TX_CPP" "data_tx_log_processed(&log, processed, n);" "DATA-TX processed trace"
require "$DATA_TX_CPP" "(void)tag;" "legacy tag parameter intentionally unused"
require "$DATA_TX_CPP" "data_tx_log_message(&log, \"EOF, Client zu\");" "DATA-TX EOF trace"
require "$DAEMON" "const char *log_ctx;" "DATA-TX context field"
require "$DAEMON" "daemon_data_tx_trace_message" "DATA-TX daemon bridge"
require "$DAEMON" "daemon_data_tx_log(\"TX433\")" "TX433 log context"
require "$DAEMON" "daemon_data_tx_log(\"TX868\")" "TX868 log context"
require "$DAEMON" "daemon_debug_ctx(tx->log_ctx, \"CAD prüfen\")" "CAD check trace"
require "$DAEMON" "daemon_debug_ctx(tx->log_ctx, \"Chunk %zu Byte Offset %zu\", len, offset)" "chunk trace"
require "$DAEMON" "daemon_debug_ctx(tx->log_ctx, \"Chunk gesendet\")" "chunk sent trace"
require "$TEST_DATA_TX" "static DataTxLog null_data_tx_log(void)" "DATA-TX null log helper"
require "$TEST_DATA_TX" "null_data_tx_log());" "DATA-TX tests pass null log callback"

forbid "$DATA_TX_CPP" "[DEBUG %s]" "old visible DEBUG tag"
forbid "$DAEMON" "  -> Sende Chunk:" "old normal chunk log"

exit "$rc"
