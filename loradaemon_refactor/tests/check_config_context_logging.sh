#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REFACTOR_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
DAEMON="$REFACTOR_DIR/loradaemon_320_108.cpp"
DISPATCH="$REFACTOR_DIR/config_dispatch.h"
TEST="$REFACTOR_DIR/tests/test_config_dispatch.cpp"

rc=0

echo "Checking CONFIG context debug logging..."

require() {
  local file="$1"
  local pattern="$2"
  local label="$3"

  if ! grep -Fq -- "$pattern" "$file"; then
    echo "ERROR: missing CONFIG log pattern: $label" >&2
    rc=1
  fi
}

require "$DISPATCH" "typedef void (*ConfigDispatchLogFn)(void *ctx, const char *msg);" "message callback typedef"
require "$DISPATCH" "typedef void (*ConfigDispatchLineLogFn)(void *ctx," "line callback typedef"
require "$DISPATCH" "ConfigDispatchLog log;" "dispatch context log field"
require "$DISPATCH" "config_dispatch_log_line(&ctx->log, \"Zeile\", line);" "line trace"
require "$DISPATCH" "config_dispatch_log_message(&ctx->log, \"Radio nicht bereit\");" "radio not ready trace"
require "$DISPATCH" "config_dispatch_log_message(&ctx->log, \"Apply startet\");" "apply start trace"
require "$DISPATCH" "config_dispatch_log_message(&ctx->log, \"Callback neu gesetzt\");" "callback restore trace"
require "$DISPATCH" "config_dispatch_log_message(&ctx->log, \"RX neu gestartet\");" "rx restart trace"
require "$DISPATCH" "config_dispatch_log_slot(&log, index, \"Client bereit\");" "client ready trace"
require "$DISPATCH" "config_dispatch_log_bytes(&log, n);" "read bytes trace"
require "$DISPATCH" "config_dispatch_log_slot(&log, index, \"EOF, Stream flush\");" "EOF trace"
require "$DISPATCH" "config_dispatch_log_slot(&log, index, \"Stream zu lang, Client zu\");" "stream too long trace"
require "$DAEMON" "daemon_config_trace_message" "daemon config message bridge"
require "$DAEMON" "daemon_config_trace_line" "daemon config line bridge"
require "$DAEMON" "daemon_config_log(\"CONFIG433\")" "433 config context"
require "$DAEMON" "daemon_config_log(\"CONFIG868\")" "868 config context"
require "$TEST" "ConfigDispatchLog log = {" "test null log"

exit "$rc"
