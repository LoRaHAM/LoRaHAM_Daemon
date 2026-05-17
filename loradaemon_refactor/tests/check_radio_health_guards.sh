#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REFACTOR_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
DAEMON="$REFACTOR_DIR/loradaemon_320_108.cpp"
CONFIG_DISPATCH="$REFACTOR_DIR/config_dispatch.h"

rc=0

echo "Checking radio health guards..."

require() {
  local file="$1"
  local pattern="$2"
  local label="$3"

  if ! grep -Fq -- "$pattern" "$file"; then
    echo "ERROR: missing radio health guard: $label" >&2
    rc=1
  fi
}

require "$DAEMON" "radio_controller_433.health" "433 controller health state"
require "$DAEMON" "radio_controller_868.health" "868 controller health state"
require "$DAEMON" "TX_RESULT_RADIO_NOT_READY" "TX not-ready result"
require "$DAEMON" "if (!daemon_radio_ready(band))" "generic radio-ready guard"
require "$DAEMON" "radio_controller_433.health = RADIO_HEALTH_READY" "433 begin success"
require "$DAEMON" "radio_controller_433.health = RADIO_HEALTH_FAILED" "433 begin failure"
require "$DAEMON" "radio_controller_868.health = RADIO_HEALTH_READY" "868 begin success"
require "$DAEMON" "radio_controller_868.health = RADIO_HEALTH_FAILED" "868 begin failure"
require "$DAEMON" "radio_controller_ready(&radio_controller_433)" "433 init/start guard"
require "$DAEMON" "radio_controller_ready(&radio_controller_868)" "868 init/start guard"
require "$DAEMON" "DATA-TX abgebrochen: RADIO_NOT_READY" "DATA TX health guard"
require "$CONFIG_DISPATCH" "radio_controller_ready(ctx->ctrl)" "CONFIG dispatch controller health guard"
require "$CONFIG_DISPATCH" "radio_controller_health(ctx->ctrl)" "CONFIG dispatch controller health name"
require "$CONFIG_DISPATCH" "CONFIG ignored" "CONFIG ignored log"

require "$REFACTOR_DIR/build.sh" "radio_health.cpp" "radio health linked in build"

if ! grep -Fq '"$SCRIPT_DIR/radio_health.cpp" \' "$REFACTOR_DIR/build.sh"; then
  echo "ERROR: radio_health.cpp in build.sh must keep line continuation." >&2
  rc=1
fi

exit "$rc"
