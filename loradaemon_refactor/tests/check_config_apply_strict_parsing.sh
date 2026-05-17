#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REFACTOR_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"

files=(
  "$REFACTOR_DIR/config_apply.cpp"
  "$REFACTOR_DIR/config_apply.h"
)

rc=0

echo "Checking CONFIG apply uses strict value parsers..."

if grep -nE '\b(atoi|atof|strtoul|strtol|strtof)\s*\(' "${files[@]}"; then
  echo "ERROR: CONFIG apply still uses partial C parsers." >&2
  rc=1
fi

if ! grep -q 'config_value_parse_' "$REFACTOR_DIR/config_apply.cpp"; then
  echo "ERROR: config_apply.cpp does not use config_value parse helpers." >&2
  rc=1
fi

if ! grep -q 'config_value_parse_bool01_exact' "$REFACTOR_DIR/config_apply.h"; then
  echo "ERROR: config_apply.h does not strictly parse GETRSSI." >&2
  rc=1
fi

exit "$rc"
