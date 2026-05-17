#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REFACTOR_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
DAEMON="$REFACTOR_DIR/loradaemon_320_108.cpp"

rc=0

echo "Checking RadioController shutdown lifecycle..."

require() {
  local pattern="$1"
  local label="$2"

  if ! grep -Fq -- "$pattern" "$DAEMON"; then
    echo "ERROR: missing RadioController shutdown lifecycle: $label" >&2
    rc=1
  fi
}

forbid() {
  local pattern="$1"
  local label="$2"

  if grep -Fq -- "$pattern" "$DAEMON"; then
    echo "ERROR: obsolete or unsafe shutdown lifecycle remains: $label" >&2
    rc=1
  fi
}

require "static void daemon_radio_shutdown_cleanup(void);" "shutdown prototype before generic cleanup"
require "daemon_radio_shutdown_cleanup();" "daemon shutdown calls radio cleanup"
require "static void radio_controller_shutdown(RadioController<RadioT> *ctrl)" "templated controller shutdown helper"
require "ctrl->radio->clearPacketReceivedAction();" "shutdown clears RX callback"
require "ctrl->radio->standby();" "shutdown puts ready radio into standby"
require "ctrl->radio->clearIrq(0xFFFFFFFF);" "shutdown clears IRQ flags"
require "delete ctrl->radio;" "shutdown deletes radio first"
require "delete ctrl->mod;" "shutdown deletes module second"
require "delete ctrl->hal;" "shutdown deletes HAL last"
require "ctrl->radio = nullptr;" "shutdown nulls radio pointer"
require "ctrl->mod = nullptr;" "shutdown nulls module pointer"
require "ctrl->hal = nullptr;" "shutdown nulls HAL pointer"
require "radio_controller_shutdown(&radio_controller_433);" "433 controller shutdown"
require "radio_controller_shutdown(&radio_controller_868);" "868 controller shutdown"
require "ctrl->health = RADIO_HEALTH_UNINITIALIZED;" "shutdown resets health"
require "ctrl->received = false;" "shutdown resets received flag"
require "ctrl->tx_busy = false;" "shutdown resets TX busy flag"
require "ctrl->cad_active = false;" "shutdown resets CAD flag"
require "ctrl->getrssi_active = false;" "shutdown resets RSSI flag"


exit "$rc"
