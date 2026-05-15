#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

CC="${CC:-gcc}"
CFLAGS="-Wall -Wextra -std=c11 -I loraham_kiss_tnc ${CFLAGS_EXTRA:-}"
LDFLAGS="${LDFLAGS:-} ${LDFLAGS_EXTRA:-}"

echo "[TEST] config"
$CC $CFLAGS \
  -o /tmp/test_lhkt_config \
  loraham_kiss_tnc/loraham_kiss_tnc.c \
  loraham_kiss_tnc/config.c \
  loraham_kiss_tnc/tests/test_config.c
/tmp/test_lhkt_config

echo "[TEST] loraham_sock"
$CC $CFLAGS \
  -o /tmp/test_lhkt_loraham_sock \
  loraham_kiss_tnc/loraham_kiss_tnc.c \
  loraham_kiss_tnc/loraham_sock.c \
  loraham_kiss_tnc/tests/test_loraham_sock.c \
  $LDFLAGS
/tmp/test_lhkt_loraham_sock

echo "[TEST] tnc2"
$CC $CFLAGS \
  -o /tmp/test_lhkt_tnc2 \
  loraham_kiss_tnc/ax25.c \
  loraham_kiss_tnc/tnc2.c \
  loraham_kiss_tnc/tests/test_tnc2.c \
  $LDFLAGS
/tmp/test_lhkt_tnc2

echo "[TEST] ax25"
$CC $CFLAGS \
  -o /tmp/test_lhkt_ax25 \
  loraham_kiss_tnc/ax25.c \
  loraham_kiss_tnc/tests/test_ax25.c \
  $LDFLAGS
/tmp/test_lhkt_ax25

echo "[TEST] kiss"
$CC $CFLAGS \
  -o /tmp/test_lhkt_kiss \
  loraham_kiss_tnc/kiss.c \
  loraham_kiss_tnc/tests/test_kiss.c \
  $LDFLAGS
/tmp/test_lhkt_kiss


echo "[TEST] cli"
$CC $CFLAGS \
  -o /tmp/test_lhkt_cli \
  loraham_kiss_tnc/loraham_kiss_tnc.c \
  loraham_kiss_tnc/config.c \
  loraham_kiss_tnc/cli.c \
  loraham_kiss_tnc/tests/test_cli.c \
  $LDFLAGS
/tmp/test_lhkt_cli

echo "[OK] All tests passed"
