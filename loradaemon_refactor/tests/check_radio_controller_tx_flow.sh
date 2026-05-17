#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REFACTOR_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
DAEMON="$REFACTOR_DIR/loradaemon_320_108.cpp"

rc=0

echo "Checking RadioController TX flow routing..."

require() {
  local pattern="$1"
  local label="$2"

  if ! grep -Fq -- "$pattern" "$DAEMON"; then
    echo "ERROR: missing RadioController TX routing: $label" >&2
    rc=1
  fi
}

forbid() {
  local pattern="$1"
  local label="$2"

  if grep -Fq -- "$pattern" "$DAEMON"; then
    echo "ERROR: obsolete TX legacy routing still present: $label" >&2
    rc=1
  fi
}

require "lora_send_controller(&radio_controller_433" "433 lora_send wrapper"
require "lora_send_controller(&radio_controller_868" "868 lora_send wrapper"
require "ctrl->radio->transmit" "TX transmit through controller radio"
require "ctrl->radio->clearPacketReceivedAction" "TX callback clear through controller radio"
require "ctrl->radio->setPacketReceivedAction(ctrl->rx_callback)" "TX callback restore through controller"
require "ctrl->radio->startReceive" "TX RX restart through controller radio"
require "ctrl->tx_busy = true" "TX busy set on controller"
require "ctrl->tx_busy = false" "TX busy clear on controller"
require "ctrl->received = false" "TX received flag clear on controller"
require "ctrl->mode == RADIO_MODE_LORA" "TX mode through controller"
require "ctrl->band == RADIO_BAND_433" "TX band-specific behavior through controller"
require "data_tx_modem_status(DataTxDaemonContext *tx)" "DATA-TX modem status uses TX context"
require "tx->radio.read_modem_status(tx->radio.modem_status_ctx)" "DATA-TX modem status uses controller callback"

forbid "data_tx_modem_status(int band)" "DATA-TX modem status integer-band helper"
forbid "txBusy433 = true" "433 legacy TX busy authoritative set"
forbid "txBusy868 = true" "868 legacy TX busy authoritative set"
forbid "radio_433->transmit" "433 transmit via raw global"
forbid "radio_868->transmit" "868 transmit via raw global"
forbid "radio_433->clearPacketReceivedAction" "433 raw callback clear"
forbid "radio_868->clearPacketReceivedAction" "868 raw callback clear"
require "ctrl->radio->setPacketReceivedAction(ctrl->rx_callback)" "TX restores callback via controller"

exit "$rc"
