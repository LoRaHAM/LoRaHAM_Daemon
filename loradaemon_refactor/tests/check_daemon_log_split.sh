#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REFACTOR_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
DAEMON="$REFACTOR_DIR/loradaemon_320_108.cpp"
LOG_H="$REFACTOR_DIR/daemon_log.h"
LOG_CPP="$REFACTOR_DIR/daemon_log.cpp"
BUILD="$REFACTOR_DIR/build.sh"

rc=0

echo "Checking daemon logger split..."

require() {
  local file="$1"
  local pattern="$2"
  local label="$3"

  if ! grep -Fq -- "$pattern" "$file"; then
    echo "ERROR: missing logger split pattern: $label" >&2
    rc=1
  fi
}

forbid() {
  local file="$1"
  local pattern="$2"
  local label="$3"

  if grep -Fq -- "$pattern" "$file"; then
    echo "ERROR: obsolete logger split pattern remains: $label" >&2
    rc=1
  fi
}

require "$DAEMON" "#include \"daemon_log.h\"" "daemon uses logger API"
require "$LOG_H" "extern DaemonLogLevel daemon_log_level;" "extern log level"
require "$LOG_H" "void daemon_debug_ctx(const char *ctx, const char *fmt, ...);" "debug declaration"
require "$LOG_CPP" "#include \"daemon_log.h\"" "logger implementation include"
require "$LOG_CPP" "DaemonLogLevel daemon_log_level = DAEMON_LOG_NORMAL;" "log level definition"
require "$LOG_CPP" "void daemon_debug_ctx(const char *ctx, const char *fmt, ...)" "debug implementation"
require "$BUILD" "daemon_log.cpp" "build links logger implementation"

forbid "$LOG_H" "static DaemonLogLevel daemon_log_level" "header-only log state"
forbid "$LOG_H" "static void daemon_debug_ctx" "header-only debug implementation"
forbid "$DAEMON" "static void daemon_debug_ctx" "inline daemon debug implementation"

exit "$rc"
