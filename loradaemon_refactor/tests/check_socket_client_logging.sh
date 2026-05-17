#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REFACTOR_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
DAEMON="$REFACTOR_DIR/loradaemon_320_108.cpp"

rc=0

echo "Checking socket/client context logging..."

require() {
  local pattern="$1"
  local label="$2"

  if ! grep -Fq -- "$pattern" "$DAEMON"; then
    echo "ERROR: missing socket/client log pattern: $label" >&2
    rc=1
  fi
}

require "static int daemon_client_slot_count(ClientSlot *slots, int max_clients)" "client count helper"
require "static bool daemon_client_slots_output_ready(ClientSlot *slots," "output-ready helper"
require "daemon_debug_ctx(ctx, \"%s-Client verbunden (%d)\", kind, after)" "accept success log"
require "daemon_debug_ctx(ctx, \"%s-Annahme ohne neuen Client\", kind)" "accept no-slot/fail log"
require "daemon_debug_ctx(ctx, \"DATA-Annahme bereit\")" "data accept ready log"
require "daemon_debug_ctx(ctx, \"CONF-Annahme bereit\")" "conf accept ready log"
require "daemon_accept_channel_logged(&channel_433, readfds, \"CLIENT433\")" "433 accept context"
require "daemon_accept_channel_logged(&channel_868, readfds, \"CLIENT868\")" "868 accept context"
require "daemon_debug_ctx(ctx, \"DATA-Ausgabe bereit\")" "data output ready log"
require "daemon_debug_ctx(ctx, \"CONF-Ausgabe bereit\")" "conf output ready log"
require "daemon_flush_channel_logged(&channel_433, readfds, \"CLIENT433\")" "433 flush context"
require "daemon_flush_channel_logged(&channel_868, readfds, \"CLIENT868\")" "868 flush context"
require "daemon_debug_ctx(\"SOCKET\", \"%d Event(s)\", ret)" "event count socket log"

exit "$rc"
