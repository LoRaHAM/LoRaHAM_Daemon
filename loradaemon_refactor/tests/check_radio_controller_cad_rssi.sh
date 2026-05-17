#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REFACTOR_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
DAEMON="$REFACTOR_DIR/loradaemon_320_108.cpp"

rc=0

echo "Checking RadioController CAD/RSSI routing..."

require() {
  local pattern="$1"
  local label="$2"

  if ! grep -Fq -- "$pattern" "$DAEMON"; then
    echo "ERROR: missing RadioController CAD/RSSI routing: $label" >&2
    rc=1
  fi
}

forbid() {
  local pattern="$1"
  local label="$2"

  if grep -Fq -- "$pattern" "$DAEMON"; then
    echo "ERROR: obsolete CAD/RSSI legacy routing still present: $label" >&2
    rc=1
  fi
}

require "daemon_process_cad_status(&radio_controller_433, &channel_433)" "433 CAD controller call"
require "daemon_process_cad_status(&radio_controller_868, &channel_868)" "868 CAD controller call"
require "daemon_process_rssi_stream_one(&radio_controller_433, &channel_433)" "433 RSSI controller call"
require "daemon_process_rssi_stream_one(&radio_controller_868, &channel_868)" "868 RSSI controller call"
require "radio_channel_read_live_rssi(ctrl->mod" "RSSI reads controller module"
require "ctrl->mode" "RSSI/CAD uses controller mode"
require "ctrl->is_hf" "RSSI uses controller HF flag"

forbid "getrssi_433_active && !txBusy433" "433 RSSI legacy condition"
forbid "getrssi_868_active && !txBusy868" "868 RSSI legacy condition"
forbid "daemon_process_cad_status(433)" "433 CAD integer call"
forbid "daemon_process_cad_status(868)" "868 CAD integer call"

exit "$rc"
