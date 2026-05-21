#!/usr/bin/env bash
set -uo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
tests_dir="$script_dir/tests"
keep_tmp=0

usage() {
  cat <<'EOF'
Usage: run_tests.sh [--keep-tmp] [--help]

Build and run the KISS/TCP TNC bridge tests.

Options:
  --keep-tmp   Keep compiled test binaries
  --help       Show this help

Environment:
  CC             C compiler, default: gcc
  CFLAGS         Full C flags override
  CFLAGS_EXTRA   Extra C flags appended to defaults
  LDFLAGS        Link flags
  LDFLAGS_EXTRA  Extra link flags appended
EOF
}

fail_usage() {
  echo "[ERR] $*" >&2
  echo
  usage >&2
  exit 2
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    --keep-tmp)
      keep_tmp=1
      shift
      ;;
    *)
      fail_usage "unknown option: $1"
      ;;
  esac
done

CC="${CC:-gcc}"
default_cflags="-Wall -Wextra -Wpedantic -Werror -std=c11 -I $script_dir"
CFLAGS="${CFLAGS:-$default_cflags}"
CFLAGS="${CFLAGS} ${CFLAGS_EXTRA:-}"
LDFLAGS="${LDFLAGS:-} ${LDFLAGS_EXTRA:-}"

tmpdir="$(mktemp -d /tmp/lhkt-tests.XXXXXX)"
ok_count=0
fail_count=0
skip_count=0

cleanup() {
  if [[ "$keep_tmp" -eq 1 ]]; then
    echo "[INFO] Keeping test binaries in $tmpdir"
  else
    rm -rf "$tmpdir"
  fi
}
trap cleanup EXIT

echo "[INFO] tmp=$tmpdir"
echo "[INFO] CC=$CC"

check_files() {
  local file

  for file in "$@"; do
    [[ -f "$file" ]] || {
      echo "[FAIL] missing source: $file"
      return 1
    }
  done

  return 0
}

run_test() {
  local name="$1"
  local bin="$tmpdir/test_$name"
  shift

  local sources=("$@")

  echo "[TEST] $name"

  if ! check_files "${sources[@]}"; then
    fail_count=$((fail_count + 1))
    return
  fi

  # shellcheck disable=SC2086
  if ! "$CC" $CFLAGS -o "$bin" "${sources[@]}" $LDFLAGS; then
    echo "[FAIL] $name build"
    fail_count=$((fail_count + 1))
    return
  fi

  if ! "$bin"; then
    echo "[FAIL] $name run"
    fail_count=$((fail_count + 1))
    return
  fi

  echo "[OK] $name"
  ok_count=$((ok_count + 1))
}

run_test config \
  "$script_dir/loraham_kiss_tnc.c" \
  "$script_dir/config.c" \
  "$tests_dir/test_config.c"

run_test loraham_sock \
  "$script_dir/loraham_kiss_tnc.c" \
  "$script_dir/loraham_sock.c" \
  "$tests_dir/test_loraham_sock.c"

run_test tnc2 \
  "$script_dir/ax25.c" \
  "$script_dir/tnc2.c" \
  "$tests_dir/test_tnc2.c"

run_test ax25 \
  "$script_dir/ax25.c" \
  "$tests_dir/test_ax25.c"

run_test kiss \
  "$script_dir/kiss.c" \
  "$tests_dir/test_kiss.c"

run_test cli \
  "$script_dir/loraham_kiss_tnc.c" \
  "$script_dir/config.c" \
  "$script_dir/cli.c" \
  "$tests_dir/test_cli.c"

# shellcheck disable=SC2086
CFLAGS_WITH_TEST="$CFLAGS -DLHKT_TEST"
old_cflags="$CFLAGS"
CFLAGS="$CFLAGS_WITH_TEST"
run_test bridge \
  "$script_dir/loraham_kiss_tnc.c" \
  "$script_dir/config.c" \
  "$script_dir/tcp_server.c" \
  "$script_dir/loraham_sock.c" \
  "$script_dir/bridge.c" \
  "$script_dir/ax25.c" \
  "$script_dir/tnc2.c" \
  "$script_dir/kiss.c" \
  "$tests_dir/test_bridge.c"
CFLAGS="$old_cflags"

echo "[SUMMARY] OK=$ok_count FAIL=$fail_count SKIP=$skip_count"

if [[ "$fail_count" -ne 0 ]]; then
  exit 1
fi

echo "[OK] All tests passed"
