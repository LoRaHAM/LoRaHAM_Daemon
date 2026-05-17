#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REFACTOR_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
DAEMON="$REFACTOR_DIR/loradaemon_320_108.cpp"
LOG_H="$REFACTOR_DIR/daemon_log.h"
LOG_CPP="$REFACTOR_DIR/daemon_log.cpp"
CONFIG_DISPATCH="$REFACTOR_DIR/config_dispatch.h"
CONFIG_APPLY="$REFACTOR_DIR/config_apply.h"
TEST_CONFIG_DISPATCH="$REFACTOR_DIR/tests/test_config_dispatch.cpp"

rc=0

echo "Checking final cleanup findings..."

require() {
  local file="$1"
  local pattern="$2"
  local label="$3"

  if ! grep -Fq -- "$pattern" "$file"; then
    echo "ERROR: missing cleanup pattern: $label" >&2
    rc=1
  fi
}

forbid() {
  local file="$1"
  local pattern="$2"
  local label="$3"

  if grep -Fq -- "$pattern" "$file"; then
    echo "ERROR: obsolete cleanup pattern remains: $label" >&2
    rc=1
  fi
}

require "$DAEMON" "#include \"daemon_log.h\"" "daemon log include"
require "$LOG_H" "void daemon_debug_ctx" "logger declaration"
require "$LOG_CPP" "void daemon_debug_ctx" "logger implementation"
forbid "$DAEMON" "static DaemonLogLevel daemon_log_level" "inline daemon log state in daemon"
forbid "$LOG_H" "static DaemonLogLevel daemon_log_level" "header-only log state"
forbid "$CONFIG_DISPATCH" "const char *prefix;" "legacy config prefix field"
forbid "$CONFIG_DISPATCH" "ctx->prefix" "legacy config prefix use"
forbid "$TEST_CONFIG_DISPATCH" "const char *prefix" "test legacy prefix arg"
forbid "$TEST_CONFIG_DISPATCH" "make_context(slots, &ctrl, NULL)" "legacy null prefix call"
forbid "$TEST_CONFIG_DISPATCH" "make_context(slots, &ctrl, \"[TEST]\")" "legacy test prefix call"
require "$CONFIG_APPLY" "bool printed = false;" "single config prefix state"
require "$DAEMON" "h = lgGpiochipOpen(0);" "single gpio open assignment"
forbid "$DAEMON" "chip = lgGpiochipOpen(0);" "duplicate gpiochip open"
require "$DAEMON" "daemon_debug_hex_bytes" "debug RX hex helper"
require "$DAEMON" "Bytes ASCII:" "normal RX ASCII output"
require "$DAEMON" "daemon_debug_ctx(rx_ctx," "RX debug metadata"
forbid "$DAEMON" "Bytes HEX" "normal RX HEX output"

exit "$rc"
