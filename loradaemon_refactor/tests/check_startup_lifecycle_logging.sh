#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REFACTOR_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
DAEMON="$REFACTOR_DIR/loradaemon_320_108.cpp"

rc=0

echo "Checking startup/lifecycle debug logging..."

require() {
  local pattern="$1"
  local label="$2"

  if ! grep -Fq -- "$pattern" "$DAEMON"; then
    echo "ERROR: missing startup/lifecycle log pattern: $label" >&2
    rc=1
  fi
}

require "daemon_debug(\"SIGPIPE wird ignoriert\")" "SIGPIPE debug log"
require "daemon_verbose(\"Daemon-Modus aktiv\")" "daemon mode verbose log"
require "daemon_verbose(\"Verbose aktiv\")" "verbose CLI log"
require "daemon_debug(\"Debug aktiv\")" "debug CLI log"
require "daemon_verbose(\"Startmodus: %s\"" "startup mode verbose log"
require "daemon_debug(\"Argumente verarbeitet\")" "argument debug log"
require "daemon_debug(\"Starte Radio- und Socket-Init\")" "startup init debug log"
require "daemon_debug(\"Startup abgeschlossen\")" "startup done debug log"
require "daemon_debug(\"Initialisiere Laufzeitkontext\")" "runtime context init debug log"
require "daemon_verbose(\"Polling aktiv\")" "polling verbose log"
require "daemon_log(\"Stop angefordert\")" "German normal stop log"
require "daemon_verbose(\"Shutdown beginnt\")" "shutdown verbose log"
require "daemon_debug(\"Stoppe Funkmodule\")" "radio shutdown debug log"
require "daemon_debug(\"Schließe Event-Backend\")" "event backend shutdown debug log"
require "daemon_debug(\"Schließe Clients\")" "client shutdown debug log"
require "daemon_debug(\"Entferne Socket-Dateien\")" "socket cleanup debug log"
require "daemon_debug(\"Shutdown abgeschlossen\")" "shutdown done debug log"

exit "$rc"
