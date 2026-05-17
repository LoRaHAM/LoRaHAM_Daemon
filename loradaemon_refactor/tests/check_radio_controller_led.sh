#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REFACTOR_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
DAEMON="$REFACTOR_DIR/loradaemon_320_108.cpp"

rc=0

echo "Checking RadioController LED routing..."

require() {
  local pattern="$1"
  local label="$2"

  if ! grep -Fq -- "$pattern" "$DAEMON"; then
    echo "ERROR: missing RadioController LED routing: $label" >&2
    rc=1
  fi
}

forbid() {
  local pattern="$1"
  local label="$2"

  if grep -Fq -- "$pattern" "$DAEMON"; then
    echo "ERROR: obsolete LED routing remains: $label" >&2
    rc=1
  fi
}

require "static void radio_controller_led(RadioController<RadioT> *ctrl, int state)" "controller LED helper"
require "lgGpioWrite(chip, ctrl->led_pin, state ? 1 : 0);" "helper uses controller LED pin"
require "static void radio_controller_flash_led(RadioController<RadioT> *ctrl)" "controller flash helper"
require "radio_controller_led(&radio_controller_433, 1);" "433 callback/init uses controller LED"
require "radio_controller_led(&radio_controller_868, 1);" "868 callback uses controller LED"
require "radio_controller_flash_led(&radio_controller_433);" "433 flash uses controller LED"
require "radio_controller_flash_led(&radio_controller_868);" "868 flash uses controller LED"
require "radio_controller_led(ctrl, 1);" "radio flows set LED via controller"
require "radio_controller_led(ctrl, 0);" "radio flows clear LED via controller"

forbid "static void data_tx_led" "band-based DATA-TX LED helper removed"
forbid "data_tx_led(" "band-based DATA-TX LED calls removed"

exit "$rc"
