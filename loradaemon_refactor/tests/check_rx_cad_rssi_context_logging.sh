#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REFACTOR_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
DAEMON="$REFACTOR_DIR/loradaemon_320_108.cpp"

rc=0

echo "Checking RX/CAD/RSSI context debug logging..."

require() {
  local pattern="$1"
  local label="$2"

  if ! grep -Fq -- "$pattern" "$DAEMON"; then
    echo "ERROR: missing RX/CAD/RSSI log pattern: $label" >&2
    rc=1
  fi
}

forbid() {
  local pattern="$1"
  local label="$2"

  if grep -Fq -- "$pattern" "$DAEMON"; then
    echo "ERROR: obsolete RX/CAD/RSSI log pattern remains: $label" >&2
    rc=1
  fi
}

require "static const char *daemon_cad_log_ctx" "CAD context helper"
require "static const char *daemon_rssi_log_ctx" "RSSI context helper"
require "static const char *daemon_rx_log_ctx" "RX context helper"
require "daemon_debug_ctx(\"RX433\", \"Flag gesetzt\")" "433 RX flag log"
require "daemon_debug_ctx(\"RX868\", \"Flag gesetzt\")" "868 RX flag log"
require "daemon_debug_ctx(ctx, \"Aktiv modem=0x%02X\", modem)" "CAD active transition"
require "daemon_debug_ctx(ctx, \"Inaktiv modem=0x%02X\", modem)" "CAD inactive transition"
require "daemon_debug_ctx(ctx, \"Auto-Stop: kein Client\")" "RSSI autostop debug"
require "daemon_debug_ctx(ctx, \"Sende %.2f dBm\", rssi)" "RSSI send debug"
require "daemon_debug_ctx(daemon_rx_log_ctx(ctrl), \"Leer-IRQ, RX neu starten\")" "empty RX debug"
require "daemon_debug_ctx(daemon_rx_log_ctx(ctrl), \"IRQ vor Read löschen\")" "pre-read IRQ debug"
require "daemon_debug_ctx(daemon_rx_log_ctx(ctrl), \"Länge %d\", len)" "packet length debug"
require "daemon_debug_ctx(daemon_rx_log_ctx(ctrl), \"readData()\")" "readData debug"
require "daemon_debug_ctx(daemon_rx_log_ctx(ctrl), \"Read OK\")" "read ok debug"
require "daemon_debug_ctx(daemon_rx_log_ctx(ctrl), \"Broadcast %d Byte\", len)" "broadcast debug"
require "daemon_debug_ctx(daemon_rx_log_ctx(ctrl), \"RX bereit\")" "rx restart debug"
require "daemon_debug_ctx(daemon_rx_log_ctx(ctrl), \"Drop %lu Status %d\"" "drop debug"

forbid "kein Client mehr verbunden -> GETRSSI auto-stop" "normal RSSI autostop print"

exit "$rc"
