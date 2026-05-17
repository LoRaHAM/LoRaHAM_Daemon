#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REFACTOR_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
DAEMON="$REFACTOR_DIR/loradaemon_320_108.cpp"

rc=0

echo "Checking that legacy radio globals are removed..."

reject_regex() {
  local pattern="$1"
  local label="$2"

  if grep -Eq -- "$pattern" "$DAEMON"; then
    echo "ERROR: legacy radio global/sync remains: $label" >&2
    rc=1
  fi
}

require() {
  local pattern="$1"
  local label="$2"

  if ! grep -Fq -- "$pattern" "$DAEMON"; then
    echo "ERROR: missing controller-owned radio state: $label" >&2
    rc=1
  fi
}

reject_regex '\bPiHal[[:space:]]*\*[[:space:]]*hal_433\b' "hal_433 global"
reject_regex '\bModule[[:space:]]*\*[[:space:]]*mod_433\b' "mod_433 global"
reject_regex '\bSX1278[[:space:]]*\*[[:space:]]*radio_433\b' "radio_433 global"
reject_regex '\bPiHal[[:space:]]*\*[[:space:]]*hal_868\b' "hal_868 global"
reject_regex '\bModule[[:space:]]*\*[[:space:]]*mod_868\b' "mod_868 global"
reject_regex '\bRFM95[[:space:]]*\*[[:space:]]*radio_868\b' "radio_868 global"
reject_regex '\bradio_433[[:space:]]*->' "radio_433 raw access"
reject_regex '\bradio_868[[:space:]]*->' "radio_868 raw access"

for pattern in \
  '\bradio_health_433\b' \
  '\bradio_health_868\b' \
  '\breceivedFlag433\b' \
  '\breceivedFlag868\b' \
  '\btxBusy433\b' \
  '\btxBusy868\b' \
  '\bcad433_active\b' \
  '\bcad868_active\b' \
  '\bgetrssi_433_active\b' \
  '\bgetrssi_868_active\b' \
  '\brx_drop_433\b' \
  '\brx_drop_868\b' \
  '\bmode_433\b' \
  '\bmode_868\b' \
  '\bruntime_433\b' \
  '\bruntime_868\b' \
  'sync_legacy_state' \
  'sync_legacy_pointers' \
  'sync_config_to_legacy_state' \
  'sync_rx_tx_from_legacy_state' \
  'sync_cad_rssi_to_legacy_state' \
  'sync_rx_to_legacy_state' \
  'sync_tx_to_legacy_state'
do
  reject_regex "$pattern" "$pattern"
done

require "radio_controller_433.health" "433 health in controller"
require "radio_controller_868.health" "868 health in controller"
require "radio_controller_433.radio = new SX1278" "433 radio pointer in controller"
require "radio_controller_868.radio = new RFM95" "868 radio pointer in controller"
require "radio_controller_433.received = true" "433 callback writes controller"
require "radio_controller_868.received = true" "868 callback writes controller"

exit "$rc"
