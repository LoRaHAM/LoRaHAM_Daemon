#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"

TEST_SRC="$SCRIPT_DIR/test_loradaemon_320_108_interface.c"
TEST_OUT="$SCRIPT_DIR/test_loradaemon_interface"

DAEMON_SRC="$SCRIPT_DIR/loradaemon_320_108.cpp"
DAEMON_OUT="$SCRIPT_DIR/loraham_daemon"

cc="${CC:-gcc}"
cxx="${CXX:-g++}"

radiolib_cflags=()
radiolib_libs=()

try_source_radiolib_dir() {
  local dir="$1"

  [[ -n "$dir" ]] || return 1
  [[ -d "$dir/src" ]] || return 1
  [[ -f "$dir/src/RadioLib.h" ]] || return 1
  [[ -f "$dir/src/hal/RPi/PiHal.h" ]] || return 1
  [[ -f "$dir/build/libRadioLib.a" ]] || return 1

  radiolib_cflags=(
    -I"$dir/src"
    -I"$dir/src/hal"
    -I"$dir/src/modules"
    -I"$dir/src/protocols/PhysicalLayer"
  )

  radiolib_libs=(
    "$dir/build/libRadioLib.a"
  )

  echo "Using RadioLib source tree: $dir"
  return 0
}

try_installed_radiolib_prefix() {
  local prefix="$1"
  local inc=""
  local lib=""

  [[ -n "$prefix" ]] || return 1

  if [[ -f "$prefix/include/RadioLib.h" && -f "$prefix/include/hal/RPi/PiHal.h" ]]; then
    inc="$prefix/include"
  elif [[ -f "$prefix/include/RadioLib/RadioLib.h" && -f "$prefix/include/RadioLib/hal/RPi/PiHal.h" ]]; then
    inc="$prefix/include/RadioLib"
  else
    return 1
  fi

  if [[ -f "$prefix/lib/libRadioLib.a" ]]; then
    lib="$prefix/lib/libRadioLib.a"
  elif [[ -f "$prefix/lib/aarch64-linux-gnu/libRadioLib.a" ]]; then
    lib="$prefix/lib/aarch64-linux-gnu/libRadioLib.a"
  elif [[ -f "$prefix/lib/libRadioLib.so" || -f "$prefix/lib/aarch64-linux-gnu/libRadioLib.so" ]]; then
    radiolib_cflags=(-I"$inc")
    radiolib_libs=(-L"$prefix/lib" -L"$prefix/lib/aarch64-linux-gnu" -lRadioLib)
    echo "Using installed RadioLib: $prefix"
    return 0
  else
    return 1
  fi

  radiolib_cflags=(-I"$inc")
  radiolib_libs=("$lib")

  echo "Using installed RadioLib: $prefix"
  return 0
}

find_radiolib() {
  if [[ -n "${RADIOLIB_DIR:-}" ]]; then
    try_source_radiolib_dir "$RADIOLIB_DIR" && return 0
    echo "ERROR: RADIOLIB_DIR is set but not usable: $RADIOLIB_DIR" >&2
    echo "Expected: src/RadioLib.h, src/hal/RPi/PiHal.h and build/libRadioLib.a" >&2
    return 1
  fi

  local candidates=(
    "$HOME/RadioLib"
    "$HOME/src/RadioLib"
    "$HOME/src/radiolib"
    "$REPO_ROOT/../RadioLib"
    "$REPO_ROOT/../../RadioLib"
    "/home/raspberry/RadioLib"
    "/home/pi/RadioLib"
  )

  for dir in "${candidates[@]}"; do
    try_source_radiolib_dir "$dir" && return 0
  done

  try_installed_radiolib_prefix "/usr/local" && return 0
  try_installed_radiolib_prefix "/usr" && return 0

  return 1
}

build_daemon() {
  if [[ ! -f "$DAEMON_SRC" ]]; then
    echo "ERROR: daemon source file not found: $DAEMON_SRC" >&2
    exit 1
  fi

  if ! find_radiolib; then
    echo "ERROR: RadioLib not found." >&2
    echo "" >&2
    echo "Try locating it:" >&2
    echo "  find \"\$HOME\" -maxdepth 4 -name libRadioLib.a 2>/dev/null" >&2
    echo "" >&2
    echo "Then run for example:" >&2
    echo "  RADIOLIB_DIR=\$HOME/src/RadioLib $0" >&2
    echo "" >&2
    echo "Or build RadioLib first:" >&2
    echo "  git clone https://github.com/jgromes/RadioLib \$HOME/src/RadioLib" >&2
    echo "  cd \$HOME/src/RadioLib" >&2
    echo "  mkdir -p build && cd build" >&2
    echo "  cmake .." >&2
    echo "  make" >&2
    exit 1
  fi

  "$cxx" \
    -std=c++11 \
    -O2 \
    -o "$DAEMON_OUT" \
    "$DAEMON_SRC" \
    "${radiolib_cflags[@]}" \
    "${radiolib_libs[@]}" \
    -llgpio

  echo "Built daemon: $DAEMON_OUT"
}

build_test() {
  if [[ ! -f "$TEST_SRC" ]]; then
    echo "ERROR: test source file not found: $TEST_SRC" >&2
    exit 1
  fi

  "$cc" \
    -std=c11 \
    -Wall \
    -Wextra \
    -O2 \
    -o "$TEST_OUT" \
    "$TEST_SRC"

  echo "Built test:   $TEST_OUT"
}

case "${1:-all}" in
  all)
    build_daemon
    build_test
    ;;
  daemon)
    build_daemon
    ;;
  test)
    build_test
    ;;
  *)
    echo "Usage: $0 [all|daemon|test]" >&2
    exit 2
    ;;
esac
