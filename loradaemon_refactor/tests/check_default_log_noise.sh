#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REFACTOR_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
DAEMON="$REFACTOR_DIR/loradaemon_320_108.cpp"
CONFIG_APPLY="$REFACTOR_DIR/config_apply.h"
CONFIG_DISPATCH="$REFACTOR_DIR/config_dispatch.h"

rc=0

echo "Checking reduced default log noise..."

require() {
  local file="$1"
  local pattern="$2"
  local label="$3"

  if ! grep -Fq -- "$pattern" "$file"; then
    echo "ERROR: missing reduced-log pattern: $label" >&2
    rc=1
  fi
}

forbid() {
  local file="$1"
  local pattern="$2"
  local label="$3"

  if grep -Fq -- "$pattern" "$file"; then
    echo "ERROR: noisy default log pattern remains: $label" >&2
    rc=1
  fi
}

require "$CONFIG_APPLY" "static inline void config_apply_print_prefix" "CONFIG prefix helper"
require "$CONFIG_APPLY" "config_apply_print_prefix(tag, &printed);" "CONFIG prefix helper use"
require "$CONFIG_DISPATCH" "printf(\"[%s] RADIO=%s CONFIG ignored\\n\"," "CONFIG not-ready single prefix"
require "$DAEMON" "\"CONF433\"," "compact 433 CONFIG tag"
require "$DAEMON" "\"CONF868\"," "compact 868 CONFIG tag"
require "$DAEMON" "static void lora_debug_tx_preview" "debug TX preview helper"
require "$DAEMON" "static void lora_debug_tx_first_bytes" "debug TX first-bytes helper"
require "$DAEMON" "lora_debug_tx_preview(tx_ctx, send_buf, len);" "TX preview behind debug"
require "$DAEMON" "daemon_debug_ctx(tx_ctx, \"Radio neu konfiguriert\")" "TX reconfig behind debug"
require "$DAEMON" "daemon_debug_ctx(tx_ctx, \"transmit OK\")" "TX success behind debug"

forbid "$CONFIG_APPLY" "printf(\"[%s] MODE=FSK -> beginFSK()\", tag);" "duplicated MODE FSK prefix"
forbid "$CONFIG_APPLY" "printf(\"[%s] MODE=LORA -> begin()\", tag);" "duplicated MODE LORA prefix"
forbid "$DAEMON" "printf(\"[SEND %d] %zu Bytes: \"" "normal SEND preview"
forbid "$DAEMON" "printf(\"[433] Radio neu konfiguriert für TX" "normal TX reconfig"
forbid "$DAEMON" "printf(\"[%s] Sende jetzt %zu Bytes\"" "normal first-bytes TX"
forbid "$DAEMON" "printf(\"[433] transmit returned OK" "normal 433 TX success"
forbid "$DAEMON" "printf(\"[868] TX OK - %zu Bytes gesendet" "normal 868 TX success"
forbid "$DAEMON" "\"CONF 433\"" "spaced 433 CONFIG tag"
forbid "$DAEMON" "\"CONF 868\"" "spaced 868 CONFIG tag"
forbid "$CONFIG_DISPATCH" "printf(\"%s\", ctx->prefix);" "dispatch prefix glue"

exit "$rc"
