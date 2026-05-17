#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REFACTOR_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"

rc=0

echo "Checking that legacy blocking broadcast helpers are absent..."

if grep -RInE '\bclient_set_send_all\s*\(' \
  "$REFACTOR_DIR" \
  --include='*.cpp' --include='*.h'; then
  echo "ERROR: legacy blocking send helper still exists." >&2
  rc=1
fi

if grep -RInE '\bclient_set_broadcast(_bytes)?\s*\(' \
  "$REFACTOR_DIR" \
  --include='*.cpp' --include='*.h' | grep -v '_queued'; then
  echo "ERROR: legacy blocking broadcast API still exists or is still used." >&2
  rc=1
fi

exit "$rc"
