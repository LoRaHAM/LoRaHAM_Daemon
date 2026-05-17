#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REFACTOR_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
DAEMON="$REFACTOR_DIR/loradaemon_320_108.cpp"

rc=0

echo "Checking RadioController RX flow routing..."

require() {
  local pattern="$1"
  local label="$2"

  if ! grep -Fq -- "$pattern" "$DAEMON"; then
    echo "ERROR: missing RadioController RX routing: $label" >&2
    rc=1
  fi
}

forbid() {
  local pattern="$1"
  local label="$2"

  if grep -Fq -- "$pattern" "$DAEMON"; then
    echo "ERROR: obsolete RX legacy routing still present: $label" >&2
    rc=1
  fi
}

require "daemon_process_radio_band(&radio_controller_433, &channel_433" "433 RX controller call"
require "daemon_process_radio_band(&radio_controller_868, &channel_868" "868 RX controller call"
require "client_slot_broadcast_bytes_queued(io->data_slots" "RX data broadcast via RadioChannelIo"
require "ctrl->radio->readData" "RX read through controller radio"
require "ctrl->radio->getPacketLength" "RX packet length through controller radio"
require "ctrl->radio->clearIrq" "RX IRQ clear through controller radio"
require "ctrl->radio->startReceive" "RX restart through controller radio"
require "ctrl->rx_drops++" "RX drop counter in controller"
require "radio_controller_packet_rssi(ctrl)" "RX packet RSSI through controller"
require "ctrl->mode == RADIO_MODE_LORA" "RX mode through controller"

forbid "daemon_process_radio_band(433" "433 integer RX call"
forbid "daemon_process_radio_band(868" "868 integer RX call"
forbid "client_slot_broadcast_bytes_queued(client_data433_slots" "433 RX data legacy ClientSlot symbol"
forbid "client_slot_broadcast_bytes_queued(client_data868_slots" "868 RX data legacy ClientSlot symbol"
forbid "daemon_rx_flag_active(int band)" "legacy RX flag accessor"
forbid "daemon_tx_busy(int band)" "legacy TX busy accessor"
forbid "daemon_rx_drop_counter(int band)" "legacy RX drop accessor"

exit "$rc"
