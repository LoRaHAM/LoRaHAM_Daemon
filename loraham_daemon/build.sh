#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
SCRIPT_NAME="${0##*/}"

DAEMON_SRC="$SCRIPT_DIR/loraham_daemon.cpp"
DAEMON_OUT="$SCRIPT_DIR/loraham_daemon"

cxx="${CXX:-g++}"
build_mode="release"
strict_build=false
clean_only=false
hardening_active=false

radiolib_cflags=()
radiolib_libs=()
hardening_ldflags=()

event_loop_sources=(
  "$SCRIPT_DIR/event_loop.cpp"
  "$SCRIPT_DIR/event_loop_epoll.cpp"
)

daemon_support_sources=(
  "$SCRIPT_DIR/daemon_log.cpp"
  "$SCRIPT_DIR/daemon_led.cpp"
  "$SCRIPT_DIR/daemon_radio_selection.cpp"
  "$SCRIPT_DIR/daemon_band.cpp"
  "$SCRIPT_DIR/hardware_profile.cpp"
  "$SCRIPT_DIR/daemon_tx_mode_boot.cpp"
  "$SCRIPT_DIR/daemon_cad_monitor_boot.cpp"
  "$SCRIPT_DIR/daemon_cad_rssi_boot.cpp"
  "$SCRIPT_DIR/daemon_high_power_boot.cpp"
  "$SCRIPT_DIR/daemon_rflog.cpp"
  "$SCRIPT_DIR/daemon_radio_runtime.cpp"
  "$SCRIPT_DIR/daemon_rx_rearm.cpp"
  "$SCRIPT_DIR/daemon_gpio_lock.cpp"
  "$SCRIPT_DIR/daemon_radio_init.cpp"
  "$SCRIPT_DIR/daemon_instance_lock.cpp"
  "$SCRIPT_DIR/unix_socket.cpp"
  "$SCRIPT_DIR/client_output_queue.cpp"
  "$SCRIPT_DIR/client_slot.cpp"
  "$SCRIPT_DIR/config_parser.cpp"
  "$SCRIPT_DIR/config_value.cpp"
  "$SCRIPT_DIR/config_policy.cpp"
  "$SCRIPT_DIR/config_validate.cpp"
  "$SCRIPT_DIR/config_stream.cpp"
  "$SCRIPT_DIR/daemon_config_runtime.cpp"
  "$SCRIPT_DIR/daemon_socket_runtime.cpp"
  "$SCRIPT_DIR/daemon_socket_dispatch.cpp"
  "$SCRIPT_DIR/daemon_io_runtime.cpp"
  "$SCRIPT_DIR/config_apply.cpp"
  "$SCRIPT_DIR/sx127x_driver.cpp"
  "$SCRIPT_DIR/sx1262_driver.cpp"
  "$SCRIPT_DIR/radio_channel.cpp"
  "$SCRIPT_DIR/daemon_timing.cpp"
  "$SCRIPT_DIR/daemon_stats.cpp"
  "$SCRIPT_DIR/daemon_lifecycle.cpp"
  "$SCRIPT_DIR/daemon_tx.cpp"
  "$SCRIPT_DIR/daemon_tx_queue.cpp"
  "$SCRIPT_DIR/daemon_tx_worker.cpp"
  "$SCRIPT_DIR/daemon_tx_async_worker.cpp"
  "$SCRIPT_DIR/daemon_tx_completion.cpp"
  "$SCRIPT_DIR/daemon_tx_executor.cpp"
  "$SCRIPT_DIR/radio_cad.cpp"
  "$SCRIPT_DIR/config_status.cpp"
  "$SCRIPT_DIR/config_dispatch.cpp"
  "$SCRIPT_DIR/daemon_tx_async_runtime.cpp"
  "$SCRIPT_DIR/daemon_data_tx_runtime.cpp"
  "$SCRIPT_DIR/data_tx.cpp"
  "$SCRIPT_DIR/framed_data.cpp"
  "$SCRIPT_DIR/framed_data_tx.cpp"
  "$SCRIPT_DIR/daemon_framed_data_runtime.cpp"
  "$SCRIPT_DIR/daemon_rx.cpp"
  "$SCRIPT_DIR/daemon_monitoring.cpp"
  "$SCRIPT_DIR/rf_packet.cpp"
  "$SCRIPT_DIR/radio_tx_limit.cpp"
  "$SCRIPT_DIR/daemon_stdout_stamp.cpp"
  "$SCRIPT_DIR/tx_result.cpp"
  "$SCRIPT_DIR/radio_health.cpp"
)

usage() {
  cat <<EOF_HELP
Usage: $SCRIPT_NAME [options]

Build the production daemon binary only.

Options:
  --output PATH        Output binary path, default: ./loraham_daemon next to build.sh
  --radiolib-dir DIR   RadioLib source tree with src/ and build/libRadioLib.a
  --debug             Build with -O0 -g instead of release defaults
  --strict            Build with -Werror
  --clean             Remove the daemon binary and exit
  -h, --help          Show this help

Environment:
  CXX                 C++ compiler, default: g++
  CXXFLAGS            Full compiler flags override
  EXTRA_CXXFLAGS      Additional compiler flags
  LDFLAGS             Additional linker flags
  RADIOLIB_DIR        Same as --radiolib-dir

Tests:
  Use ./run_tests.sh from this directory to build and run tests.
EOF_HELP
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --output)
      [[ $# -ge 2 ]] || { echo "ERROR: --output needs a path." >&2; exit 2; }
      DAEMON_OUT="$2"
      shift 2
      ;;
    --radiolib-dir)
      [[ $# -ge 2 ]] || { echo "ERROR: --radiolib-dir needs a directory." >&2; exit 2; }
      RADIOLIB_DIR="$2"
      shift 2
      ;;
    --debug)
      build_mode="debug"
      shift
      ;;
    --strict)
      strict_build=true
      shift
      ;;
    --clean|clean)
      clean_only=true
      shift
      ;;
    daemon|all)
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "ERROR: unknown option: $1" >&2
      echo >&2
      usage >&2
      exit 2
      ;;
  esac
done

require_command() {
  local cmd="$1"

  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "ERROR: required command not found: $cmd" >&2
    exit 1
  fi
}

try_source_radiolib_dir() {
  local dir="$1"

  [[ -n "$dir" ]] || return 1
  [[ -d "$dir/src" ]] || return 1
  [[ -f "$dir/src/RadioLib.h" ]] || return 1
  [[ -f "$dir/src/hal/RPi/PiHal.h" ]] || return 1
  [[ -f "$dir/build/libRadioLib.a" ]] || return 1

  radiolib_cflags=(
    -isystem "$dir/src"
    -isystem "$dir/src/hal"
    -isystem "$dir/src/modules"
    -isystem "$dir/src/protocols/PhysicalLayer"
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
    radiolib_cflags=(-isystem "$inc")
    radiolib_libs=(-L"$prefix/lib" -L"$prefix/lib/aarch64-linux-gnu" -lRadioLib)
    echo "Using installed RadioLib: $prefix"
    return 0
  else
    return 1
  fi

  radiolib_cflags=(-isystem "$inc")
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

compiler_flags() {
  local warning_flags=(
    -Wall
    -Wextra
    -Wpedantic
    -Wformat=2
    -Wformat-security
    -Werror=format-security
  )
  local release_hardening_flags=(
    -D_FORTIFY_SOURCE=2
    -fstack-protector-strong
    -fPIE
    -D_GLIBCXX_ASSERTIONS
  )

  hardening_active=false
  hardening_ldflags=()

  if [[ -n "${CXXFLAGS:-}" ]]; then
    # shellcheck disable=SC2206
    cxxflags=($CXXFLAGS)
  elif [[ "$build_mode" == "debug" ]]; then
    cxxflags=(-std=c++20 -O0 -g "${warning_flags[@]}" -pthread)
  else
    cxxflags=(
      -std=c++20
      -O2
      -g
      "${warning_flags[@]}"
      "${release_hardening_flags[@]}"
      -pthread
    )
    hardening_ldflags=(
      -pie
      -Wl,-z,relro,-z,now,-z,noexecstack
    )
    hardening_active=true
  fi

  if [[ "$strict_build" == true ]]; then
    cxxflags+=(-Werror)
    # ...but NOT for a dependency's own #warning. RadioLib's PiHal.h warns when the system
    # lgpio is older than 0.2.2, which is true of every Debian/Ubuntu package today, so
    # -Werror turned a third-party notice into a build failure and no strict build could ever
    # run. -isystem does not help: GCC honours an explicit #warning even in a system header.
    # The notice still prints; it just no longer fails a build we do not control the cause of.
    cxxflags+=(-Wno-error=cpp)
  fi

  if [[ -n "${EXTRA_CXXFLAGS:-}" ]]; then
    # shellcheck disable=SC2206
    extra_cxxflags=($EXTRA_CXXFLAGS)
  else
    extra_cxxflags=()
  fi

  if [[ -n "${LDFLAGS:-}" ]]; then
    # shellcheck disable=SC2206
    extra_ldflags=($LDFLAGS)
  else
    extra_ldflags=()
  fi
}

print_build_config() {
  echo "Build target: $DAEMON_OUT"
  echo "Build mode:   $build_mode"
  echo "Compiler:     $cxx"
  echo "Strict build: $strict_build"

  if [[ "$hardening_active" == true ]]; then
    echo "Hardening:   enabled"
  else
    echo "Hardening:   custom or debug"
  fi
}

verify_daemon_hardening() {
  local binary="$1"
  local stack_flags=""

  [[ "$hardening_active" == true ]] || return 0

  require_command readelf
  require_command grep
  require_command awk

  if ! readelf -h "$binary" | grep -Eq 'Type:[[:space:]]+DYN'; then
    echo "ERROR: hardening check failed: PIE missing." >&2
    return 1
  fi

  if ! readelf -W -l "$binary" | grep -q 'GNU_RELRO'; then
    echo "ERROR: hardening check failed: RELRO missing." >&2
    return 1
  fi

  if ! readelf -d "$binary" | grep -Eq 'BIND_NOW|FLAGS_1.*NOW'; then
    echo "ERROR: hardening check failed: BIND_NOW missing." >&2
    return 1
  fi

  stack_flags="$(readelf -W -l "$binary" |
    awk '$1 == "GNU_STACK" { print $(NF - 1); exit }')"
  if [[ "$stack_flags" != "RW" ]]; then
    echo "ERROR: hardening check failed: executable stack." >&2
    return 1
  fi

  echo "Hardening checks: OK"
}

clean_daemon() {
  rm -f -- "$DAEMON_OUT"
  echo "Removed: $DAEMON_OUT"
}

build_daemon() {
  local out_dir
  local tmp_out

  if [[ ! -f "$DAEMON_SRC" ]]; then
    echo "ERROR: daemon source file not found: $DAEMON_SRC" >&2
    exit 1
  fi

  require_command "$cxx"

  if ! find_radiolib; then
    echo "ERROR: RadioLib not found." >&2
    echo "" >&2
    echo "Try locating it:" >&2
    echo "  find \"\$HOME\" -maxdepth 4 -name libRadioLib.a 2>/dev/null" >&2
    echo "" >&2
    echo "Then run for example:" >&2
    echo "  RADIOLIB_DIR=\$HOME/src/RadioLib $SCRIPT_DIR/$SCRIPT_NAME" >&2
    echo "" >&2
    echo "Or build RadioLib first:" >&2
    echo "  git clone https://github.com/jgromes/RadioLib \$HOME/src/RadioLib" >&2
    echo "  cd \$HOME/src/RadioLib" >&2
    echo "  mkdir -p build && cd build" >&2
    echo "  cmake .." >&2
    echo "  make" >&2
    exit 1
  fi

  compiler_flags
  print_build_config

  out_dir="$(dirname -- "$DAEMON_OUT")"
  mkdir -p -- "$out_dir"
  tmp_out="$(mktemp -- "$out_dir/.loraham_daemon.tmp.XXXXXX")"
  rm -f -- "${tmp_out:-}"

  trap 'rm -f -- "${tmp_out:-}"' EXIT

  "$cxx" \
    "${cxxflags[@]}" \
    "${extra_cxxflags[@]}" \
    -o "$tmp_out" \
    "$DAEMON_SRC" \
    "${daemon_support_sources[@]}" \
    "${event_loop_sources[@]}" \
    "${radiolib_cflags[@]}" \
    "${radiolib_libs[@]}" \
    "${hardening_ldflags[@]}" \
    "${extra_ldflags[@]}" \
    -llgpio

  verify_daemon_hardening "$tmp_out"
  chmod 755 "$tmp_out"
  mv -f -- "$tmp_out" "$DAEMON_OUT"
  trap - EXIT

  echo "Built daemon: $DAEMON_OUT"
}

if [[ "$clean_only" == true ]]; then
  clean_daemon
  exit 0
fi

build_daemon
