#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REFACTOR_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
HEADER="$REFACTOR_DIR/client_slot.h"
SOURCE="$REFACTOR_DIR/client_slot.cpp"
DISPATCH="$REFACTOR_DIR/config_dispatch.h"
CHANNEL_H="$REFACTOR_DIR/radio_channel.h"
CHANNEL_CPP="$REFACTOR_DIR/radio_channel.cpp"
DAEMON="$REFACTOR_DIR/loradaemon_320_108.cpp"

rc=0

echo "Checking ClientSlot structure..."

require() {
  local file="$1"
  local pattern="$2"
  local label="$3"

  if ! grep -Fq -- "$pattern" "$file"; then
    echo "ERROR: missing ClientSlot structure item: $label" >&2
    rc=1
  fi
}

require "$HEADER" "typedef struct {" "ClientSlot typedef"
require "$HEADER" "int fd;" "fd stored in ClientSlot"
require "$HEADER" "ClientOutputQueue output;" "output queue stored in ClientSlot"
require "$HEADER" "ConfigStreamBuffer stream;" "CONFIG stream stored in ClientSlot"
require "$SOURCE" "client_output_queue_reset(&slot->output)" "output reset with slot"
require "$SOURCE" "config_stream_init(&slot->stream)" "stream reset with slot"
require "$DISPATCH" "ClientSlot *slots" "CONFIG dispatch uses ClientSlot"
require "$DISPATCH" "slot->stream" "CONFIG dispatch uses slot stream"
require "$DISPATCH" "client_slot_close(slot)" "CONFIG dispatch closes whole slot"
require "$CHANNEL_H" "ClientSlot *conf_slots;" "RadioChannelIo stores CONF ClientSlot"
require "$CHANNEL_CPP" "client_slot_accept_with_output" "CONF accept uses ClientSlot"
require "$CHANNEL_CPP" "client_slot_broadcast_queued" "CONF broadcasts use ClientSlot"
require "$DAEMON" "ClientSlot client_conf433_slots" "daemon has 433 CONF ClientSlot table"
require "$DAEMON" "ClientSlot client_conf868_slots" "daemon has 868 CONF ClientSlot table"
require "$DAEMON" "client_slot_init_all(client_conf433_slots, MAX_CLIENTS)" "433 CONF ClientSlot init"
require "$DAEMON" "client_slot_init_all(client_conf868_slots, MAX_CLIENTS)" "868 CONF ClientSlot init"
require "$REFACTOR_DIR/build.sh" '"$SCRIPT_DIR/client_slot.cpp"' "client_slot linked in build"
require "$REFACTOR_DIR/run_tests.sh" '"$TEST_DIR/test_client_slot"' "test_client_slot in run_tests"

exit "$rc"
