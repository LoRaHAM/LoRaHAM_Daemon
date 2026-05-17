#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REFACTOR_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
DAEMON="$REFACTOR_DIR/loradaemon_320_108.cpp"
LOG_H="$REFACTOR_DIR/daemon_log.h"

rc=0

echo "Checking context-based verbose/debug prefixes..."

require() {
  local pattern="$1"
  local label="$2"

  if ! grep -Fq -- "$pattern" "$DAEMON" && ! grep -Fq -- "$pattern" "$LOG_H"; then
    echo "ERROR: missing context logging pattern: $label" >&2
    rc=1
  fi
}

forbid() {
  local pattern="$1"
  local label="$2"

  if grep -Fq -- "$pattern" "$DAEMON" || grep -Fq -- "$pattern" "$LOG_H"; then
    echo "ERROR: obsolete context logging pattern remains: $label" >&2
    rc=1
  fi
}

require "static void daemon_verbose_ctx(const char *ctx, const char *fmt, ...)" "verbose context helper"
require "static void daemon_debug_ctx(const char *ctx, const char *fmt, ...)" "debug context helper"
require "daemon_verbose_ctx(\"STARTUP\", \"Startmodus: %s\"" "startup verbose context"
require "daemon_debug_ctx(\"STARTUP\", \"Argumente verarbeitet\")" "startup debug context"
require "daemon_verbose_ctx(\"LIFE\", \"Polling aktiv\")" "life verbose context"
require "daemon_debug_ctx(\"LIFE\", \"Stoppe Funkmodule\")" "life debug context"

forbid "daemon_vlog(\"[VERBOSE]\"" "visible VERBOSE prefix"
forbid "daemon_vlog(\"[DEBUG]\"" "visible DEBUG prefix"
forbid "static void daemon_verbose(const char *fmt, ...)" "generic verbose helper"
forbid "static void daemon_debug(const char *fmt, ...)" "generic debug helper"

exit "$rc"
