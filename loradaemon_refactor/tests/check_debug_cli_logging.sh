#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REFACTOR_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
DAEMON="$REFACTOR_DIR/loradaemon_320_108.cpp"
LOG_H="$REFACTOR_DIR/daemon_log.h"
COMMON="$REFACTOR_DIR/tests/common_loradaemon_test.h"
IFACE="$REFACTOR_DIR/tests/test_interface_baseline.c"

rc=0

echo "Checking debug/verbose CLI logging foundation..."

require() {
  local file="$1"
  local pattern="$2"
  local label="$3"

  if ! grep -Fq -- "$pattern" "$file"; then
    echo "ERROR: missing debug CLI/logging pattern: $label" >&2
    rc=1
  fi
}

require "$LOG_H" "DAEMON_LOG_VERBOSE" "verbose log level"
require "$LOG_H" "DAEMON_LOG_DEBUG" "debug log level"
require "$LOG_H" "static DaemonLogLevel daemon_log_level" "global log level"
require "$LOG_H" "static void daemon_verbose_ctx(const char *ctx, const char *fmt, ...)" "context verbose helper"
require "$LOG_H" "static void daemon_debug_ctx(const char *ctx, const char *fmt, ...)" "context debug helper"
require "$LOG_H" "static void daemon_debug_band(const char *tag, const char *fmt, ...)" "band debug helper"
require "$LOG_H" "static void daemon_vlog_ctx(const char *ctx, const char *fmt, va_list ap)" "context prefix helper"
require "$DAEMON" "#include \"daemon_log.h\"" "daemon log helper include"
require "$DAEMON" "{\"verbose\", no_argument, 0, 'v'}" "long verbose option"
require "$DAEMON" "{\"debug\",   no_argument, 0, 1000}" "long debug option"
require "$DAEMON" "getopt_long(argc, argv, \"dvh\", long_options, NULL)" "getopt_long parser"
require "$DAEMON" "[-d] [-v|--verbose] [--debug] [--help]" "usage text"
require "$COMMON" "test_cli_help_option" "CLI help option test helper"
require "$IFACE" "CLI accepts -v" "short verbose interface test"
require "$IFACE" "CLI accepts --verbose" "long verbose interface test"
require "$IFACE" "CLI accepts --debug" "debug interface test"

forbid() {
  local file="$1"
  local pattern="$2"
  local label="$3"

  if grep -Fq -- "$pattern" "$file"; then
    echo "ERROR: obsolete debug CLI/logging pattern remains: $label" >&2
    rc=1
  fi
}

forbid "$DAEMON" "daemon_vlog(\"[VERBOSE]\"" "visible VERBOSE prefix"
forbid "$DAEMON" "daemon_vlog(\"[DEBUG]\"" "visible DEBUG prefix"
forbid "$DAEMON" "static void daemon_verbose(const char *fmt, ...)" "generic verbose helper"
forbid "$DAEMON" "static void daemon_debug(const char *fmt, ...)" "generic debug helper"

exit "$rc"
