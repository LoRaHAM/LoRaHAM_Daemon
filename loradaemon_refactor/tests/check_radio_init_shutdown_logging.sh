#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REFACTOR_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
DAEMON="$REFACTOR_DIR/loradaemon_320_108.cpp"

rc=0

echo "Checking RadioController init/shutdown context logging..."

require() {
  local pattern="$1"
  local label="$2"

  if ! grep -Fq -- "$pattern" "$DAEMON"; then
    echo "ERROR: missing radio init/shutdown log pattern: $label" >&2
    rc=1
  fi
}

require "daemon_debug_ctx(\"RADIO\", \"Controller initialisieren\")" "controller init debug"
require "daemon_debug_band(\"433\", \"Controller bereit\")" "433 controller init"
require "daemon_debug_band(\"868\", \"Controller bereit\")" "868 controller init"
require "daemon_verbose_ctx(\"RADIO\", \"Funk-Init beginnt\")" "radio init verbose start"
require "daemon_debug_ctx(\"RADIO\", \"Health zurückgesetzt\")" "health reset debug"
require "daemon_debug_band(\"433\", \"Objekte anlegen\")" "433 objects"
require "daemon_debug_band(\"868\", \"Objekte anlegen\")" "868 objects"
require "daemon_debug_band(\"433\", \"begin()\")" "433 begin debug"
require "daemon_debug_band(\"868\", \"begin()\")" "868 begin debug"
require "daemon_verbose_ctx(\"433\", \"Radio bereit\")" "433 ready verbose"
require "daemon_verbose_ctx(\"868\", \"Radio bereit\")" "868 ready verbose"
require "daemon_debug_band(\"433\", \"LoRa-Default gesetzt\")" "433 default debug"
require "daemon_debug_band(\"868\", \"LoRa-Default gesetzt\")" "868 default debug"
require "daemon_debug_band(\"433\", \"Callback gesetzt\")" "433 callback debug"
require "daemon_debug_band(\"868\", \"Callback gesetzt\")" "868 callback debug"
require "daemon_debug_band(\"433\", \"RX starten\")" "433 RX start debug"
require "daemon_debug_band(\"868\", \"RX starten\")" "868 RX start debug"
require "daemon_verbose_ctx(\"RADIO\", \"Funk-Init abgeschlossen\")" "radio init verbose done"
require "daemon_verbose_ctx(tag, \"Radio-Shutdown\")" "radio shutdown verbose"
require "daemon_debug_band(tag, \"Callback aus\")" "shutdown callback debug"
require "daemon_debug_band(tag, \"Standby\")" "shutdown standby debug"
require "daemon_debug_band(tag, \"IRQ löschen\")" "shutdown IRQ debug"
require "daemon_debug_band(tag, \"Radio freigeben\")" "shutdown radio release debug"
require "daemon_debug_band(tag, \"Zustand zurückgesetzt\")" "shutdown reset debug"
require "daemon_debug_ctx(\"CLIENT\", \"Slots initialisieren\")" "client slot init debug"
require "daemon_debug_ctx(\"RADIO\", \"Kanal-IO initialisieren\")" "channel io init debug"
require "daemon_debug_ctx(\"SOCKET\", \"Socket-Dateien öffnen\")" "socket open debug"
require "daemon_debug_ctx(\"GPIO\", \"LED initialisieren\")" "LED init debug"
require "daemon_debug_ctx(\"RADIO\", \"RadioLib initialisieren\")" "RadioLib init debug"

exit "$rc"
