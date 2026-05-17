#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REFACTOR_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
APPLY="$REFACTOR_DIR/config_apply.h"
PARSER="$REFACTOR_DIR/config_parser.h"
VALIDATE="$REFACTOR_DIR/config_validate.cpp"

rc=0

echo "Checking transactional CONFIG apply wiring..."

require() {
  local file="$1"
  local pattern="$2"
  local label="$3"

  if ! grep -Fq -- "$pattern" "$file"; then
    echo "ERROR: missing transactional CONFIG guard: $label" >&2
    rc=1
  fi
}

require "$APPLY" "#include \"config_validate.h\"" "config validation include"
require "$APPLY" "config_validate_command(parsed, mode_flag, &validation)" "whole-command validation before apply"
require "$APPLY" "CONFIG rejected" "reject log"
require "$PARSER" "malformed_tokens" "parser reports malformed tokens"
require "$VALIDATE" "cmd.malformed_tokens" "validation rejects malformed tokens"
require "$REFACTOR_DIR/build.sh" '"$SCRIPT_DIR/config_validate.cpp"' "config_validate in build"
require "$REFACTOR_DIR/run_tests.sh" '"$TEST_DIR/test_config_validate"' "config_validate test in run_tests"
require "$REFACTOR_DIR/run_tests.sh" '"$TEST_DIR/test_config_apply_transactional"' "config apply transactional test in run_tests"

validation_line=$(grep -n 'config_validate_command(parsed, mode_flag, &validation)' "$APPLY" | head -n1 | cut -d: -f1 || true)
getrssi_line=$(grep -n 'if(key == "GETRSSI")' "$APPLY" | head -n1 | cut -d: -f1 || true)
mode_line=$(grep -n 'if(!mode_val.empty())' "$APPLY" | head -n1 | cut -d: -f1 || true)

if [[ -z "$validation_line" || -z "$getrssi_line" || -z "$mode_line" ]]; then
  echo "ERROR: could not locate validation/GETRSSI/MODE lines." >&2
  rc=1
elif [[ "$validation_line" -gt "$getrssi_line" || "$validation_line" -gt "$mode_line" ]]; then
  echo "ERROR: validation must happen before GETRSSI and MODE side effects." >&2
  rc=1
fi

exit "$rc"
