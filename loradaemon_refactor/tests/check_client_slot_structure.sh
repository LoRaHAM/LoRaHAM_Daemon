#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REFACTOR_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
HEADER="$REFACTOR_DIR/client_slot.h"
SOURCE="$REFACTOR_DIR/client_slot.cpp"

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
require "$REFACTOR_DIR/build.sh" '"$SCRIPT_DIR/client_slot.cpp"' "client_slot linked in build"
require "$REFACTOR_DIR/run_tests.sh" '"$TEST_DIR/test_client_slot"' "test_client_slot in run_tests"

exit "$rc"
