#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
TEST_DIR="$SCRIPT_DIR/tests"
DAEMON_BIN="$SCRIPT_DIR/loraham_daemon"

tx_tests=false
rx_seconds="${RX_SECONDS:-15}"

cc="${CC:-gcc}"
cxx="${CXX:-g++}"

section() {
  local title="$1"

  echo
  echo "================================================================"
  echo "$title"
  echo "================================================================"
}

require_command() {
  local cmd="$1"

  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "ERROR: required command not found: $cmd" >&2
    exit 1
  fi
}

require_positive_int() {
  local value="$1"
  local label="$2"

  if [[ ! "$value" =~ ^[1-9][0-9]*$ ]]; then
    echo "ERROR: $label must be a positive integer." >&2
    exit 2
  fi
}

radiolib_cflags=()
radiolib_libs=()

event_loop_sources=(
  "$SCRIPT_DIR/event_loop.cpp"
  "$SCRIPT_DIR/event_loop_epoll.cpp"
)

daemon_support_sources=(
  "$SCRIPT_DIR/daemon_log.cpp"
  "$SCRIPT_DIR/unix_socket.cpp"
  "$SCRIPT_DIR/client_set.cpp"
  "$SCRIPT_DIR/client_slot.cpp"
  "$SCRIPT_DIR/config_parser.cpp"
  "$SCRIPT_DIR/config_value.cpp"
  "$SCRIPT_DIR/config_policy.cpp"
  "$SCRIPT_DIR/config_validate.cpp"
  "$SCRIPT_DIR/config_stream.cpp"
  "$SCRIPT_DIR/config_apply.cpp"
  "$SCRIPT_DIR/radio_channel.cpp"
  "$SCRIPT_DIR/daemon_timing.cpp"
  "$SCRIPT_DIR/daemon_lifecycle.cpp"
  "$SCRIPT_DIR/data_tx.cpp"
  "$SCRIPT_DIR/rf_packet.cpp"
  "$SCRIPT_DIR/tx_result.cpp"
  "$SCRIPT_DIR/radio_health.cpp"
)

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


build_one_test() {
  local src="$1"
  local out="$2"

  "$cc" \
    -std=c11 \
    -Wall \
    -Wextra \
    -O2 \
    -I"$TEST_DIR" \
    -o "$out" \
    "$src"

  echo "Built test:   $out"
}

build_one_cpp_sources() {
  local out="$1"
  shift

  "$cxx" \
    -std=c++11 \
    -Wall \
    -Wextra \
    -O2 \
    -I"$TEST_DIR" \
    -I"$SCRIPT_DIR" \
    -o "$out" \
    "$@"

  echo "Built test:   $out"
}

build_one_cpp_test() {
  local src="$1"
  local out="$2"

  build_one_cpp_sources \
    "$out" \
    "$src" \
    "$SCRIPT_DIR/config_parser.cpp"
}

build_one_config_stream_buffer_test() {
  local src="$1"
  local out="$2"

  build_one_cpp_sources \
    "$out" \
    "$src" \
    "$SCRIPT_DIR/config_stream.cpp"
}

build_one_unix_socket_test() {
  local src="$1"
  local out="$2"

  build_one_cpp_sources \
    "$out" \
    "$src" \
    "$SCRIPT_DIR/unix_socket.cpp"
}

build_one_lifecycle_test() {
  local src="$1"
  local out="$2"

  build_one_cpp_sources \
    "$out" \
    "$src" \
    "$SCRIPT_DIR/daemon_lifecycle.cpp"
}

build_one_timing_test() {
  local src="$1"
  local out="$2"

  build_one_cpp_sources \
    "$out" \
    "$src" \
    "$SCRIPT_DIR/daemon_timing.cpp"
}

build_one_event_loop_test() {
  local src="$1"
  local out="$2"

  build_one_cpp_sources \
    "$out" \
    "$src" \
    "${event_loop_sources[@]}"
}

build_one_data_tx_test() {
  local src="$1"
  local out="$2"

  build_one_cpp_sources \
    "$out" \
    "$src" \
    "$SCRIPT_DIR/data_tx.cpp" \
    "$SCRIPT_DIR/client_slot.cpp" \
    "$SCRIPT_DIR/config_stream.cpp" \
    "$SCRIPT_DIR/client_set.cpp" \
    "${event_loop_sources[@]}"
}


build_one_client_output_queue_test() {
  local src="$1"
  local out="$2"

  build_one_cpp_sources \
    "$out" \
    "$src" \
    "$SCRIPT_DIR/client_set.cpp" \
    "${event_loop_sources[@]}"
}

build_one_client_nonblocking_test() {
  local src="$1"
  local out="$2"

  build_one_cpp_sources \
    "$out" \
    "$src" \
    "$SCRIPT_DIR/client_set.cpp" \
    "${event_loop_sources[@]}"
}

build_one_client_queued_broadcast_test() {
  local src="$1"
  local out="$2"

  build_one_cpp_sources \
    "$out" \
    "$src" \
    "$SCRIPT_DIR/client_set.cpp" \
    "${event_loop_sources[@]}"
}

build_one_client_slow_output_test() {
  local src="$1"
  local out="$2"

  build_one_cpp_sources \
    "$out" \
    "$src" \
    "$SCRIPT_DIR/client_set.cpp" \
    "${event_loop_sources[@]}"
}

build_one_event_loop_output_flush_test() {
  local src="$1"
  local out="$2"

  build_one_cpp_sources \
    "$out" \
    "$src" \
    "$SCRIPT_DIR/client_set.cpp" \
    "${event_loop_sources[@]}"
}


build_one_rf_packet_test() {
  local src="$1"
  local out="$2"

  build_one_cpp_sources \
    "$out" \
    "$src" \
    "$SCRIPT_DIR/rf_packet.cpp"
}





build_one_radio_health_test() {
  local src="$1"
  local out="$2"

  build_one_cpp_sources \
    "$out" \
    "$src" \
    "$SCRIPT_DIR/radio_health.cpp"
}

build_one_tx_result_test() {
  local src="$1"
  local out="$2"

  build_one_cpp_sources \
    "$out" \
    "$src" \
    "$SCRIPT_DIR/tx_result.cpp"
}



build_one_config_apply_transactional_test() {
  local src="$1"
  local out="$2"

  if [[ "${#radiolib_cflags[@]}" -eq 0 ]]; then
    if ! find_radiolib; then
      echo "ERROR: RadioLib not found for config apply transactional test." >&2
      exit 1
    fi
  fi

  build_one_cpp_sources \
    "$out" \
    "${radiolib_cflags[@]}" \
    "$src" \
    "$SCRIPT_DIR/config_parser.cpp" \
    "$SCRIPT_DIR/config_validate.cpp" \
    "$SCRIPT_DIR/config_value.cpp" \
    "$SCRIPT_DIR/config_policy.cpp"
}

build_one_config_validate_test() {
  local src="$1"
  local out="$2"

  build_one_cpp_sources \
    "$out" \
    "$src" \
    "$SCRIPT_DIR/config_validate.cpp" \
    "$SCRIPT_DIR/config_parser.cpp" \
    "$SCRIPT_DIR/config_value.cpp" \
    "$SCRIPT_DIR/config_policy.cpp"
}

build_one_config_policy_test() {
  local src="$1"
  local out="$2"

  build_one_cpp_sources \
    "$out" \
    "$src" \
    "$SCRIPT_DIR/config_policy.cpp" \
    "$SCRIPT_DIR/config_value.cpp"
}

build_one_config_value_test() {
  local src="$1"
  local out="$2"

  build_one_cpp_sources \
    "$out" \
    "$src" \
    "$SCRIPT_DIR/config_value.cpp"
}

build_one_config_dispatch_test() {
  local src="$1"
  local out="$2"

  if [[ "${#radiolib_cflags[@]}" -eq 0 ]]; then
    if ! find_radiolib; then
      echo "ERROR: RadioLib not found for config dispatch test." >&2
      exit 1
    fi
  fi

  build_one_cpp_sources \
    "$out" \
    "${radiolib_cflags[@]}" \
    "$src" \
    "$SCRIPT_DIR/client_set.cpp" \
    "$SCRIPT_DIR/radio_health.cpp" \
    "$SCRIPT_DIR/config_stream.cpp" \
    "${event_loop_sources[@]}" \
    "$SCRIPT_DIR/client_slot.cpp"
}

build_tests() {
  build_one_data_tx_test "$TEST_DIR/test_data_tx.cpp" "$TEST_DIR/test_data_tx"
  build_one_client_output_queue_test "$TEST_DIR/test_client_output_queue.cpp" "$TEST_DIR/test_client_output_queue"
  build_one_client_nonblocking_test "$TEST_DIR/test_client_nonblocking.cpp" "$TEST_DIR/test_client_nonblocking"
  build_one_client_queued_broadcast_test "$TEST_DIR/test_client_queued_broadcast.cpp" "$TEST_DIR/test_client_queued_broadcast"
  build_one_client_slow_output_test "$TEST_DIR/test_client_slow_output.cpp" "$TEST_DIR/test_client_slow_output"
  build_one_event_loop_output_flush_test "$TEST_DIR/test_event_loop_output_flush.cpp" "$TEST_DIR/test_event_loop_output_flush"
  build_one_tx_result_test "$TEST_DIR/test_tx_result.cpp" "$TEST_DIR/test_tx_result"
  build_one_radio_health_test "$TEST_DIR/test_radio_health.cpp" "$TEST_DIR/test_radio_health"
  build_one_rf_packet_test "$TEST_DIR/test_rf_packet.cpp" "$TEST_DIR/test_rf_packet"
  build_one_event_loop_test "$TEST_DIR/test_event_loop.cpp" "$TEST_DIR/test_event_loop"
  build_one_timing_test "$TEST_DIR/test_daemon_timing.cpp" "$TEST_DIR/test_daemon_timing"
  build_one_lifecycle_test "$TEST_DIR/test_daemon_lifecycle.cpp" "$TEST_DIR/test_daemon_lifecycle"
  build_one_unix_socket_test "$TEST_DIR/test_unix_socket.cpp" "$TEST_DIR/test_unix_socket"
  build_one_test "$TEST_DIR/test_rssi_multiclient.c" "$TEST_DIR/test_rssi_multiclient"
  build_one_cpp_test "$TEST_DIR/test_config_parser.cpp" "$TEST_DIR/test_config_parser"
  build_one_config_stream_buffer_test "$TEST_DIR/test_config_stream_buffer.cpp" "$TEST_DIR/test_config_stream_buffer"
  build_one_config_value_test "$TEST_DIR/test_config_value.cpp" "$TEST_DIR/test_config_value"
  build_one_config_policy_test "$TEST_DIR/test_config_policy.cpp" "$TEST_DIR/test_config_policy"
  build_one_config_validate_test "$TEST_DIR/test_config_validate.cpp" "$TEST_DIR/test_config_validate"
  build_one_config_apply_transactional_test "$TEST_DIR/test_config_apply_transactional.cpp" "$TEST_DIR/test_config_apply_transactional"
  build_one_config_dispatch_test "$TEST_DIR/test_config_dispatch.cpp" "$TEST_DIR/test_config_dispatch"
  build_one_test "$TEST_DIR/test_interface_baseline.c" "$TEST_DIR/test_interface_baseline"
  build_one_test "$TEST_DIR/test_config_stream.c" "$TEST_DIR/test_config_stream"
  build_one_test "$TEST_DIR/test_client_lifecycle.c" "$TEST_DIR/test_client_lifecycle"
}

usage() {
  cat <<EOF_HELP
Usage: run_tests.sh [--TX] [--rx-seconds N]

Builds the daemon, builds test binaries, then runs each test with its own daemon.

Options:
  --TX             Run RF transmit tests too
  --rx-seconds N   RX observation time for --TX, default: 15
  -h, --help       Show this help
EOF_HELP
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --TX)
      tx_tests=true
      shift
      ;;
    --rx-seconds)
      if [[ $# -lt 2 ]]; then
        echo "ERROR: --rx-seconds needs a value." >&2
        exit 2
      fi
      rx_seconds="$2"
      shift 2
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

require_positive_int "$rx_seconds" "rx-seconds"

section "Pre-flight"
require_command "$cc"
require_command "$cxx"
require_command pgrep
require_command pkill
require_command sleep
require_command grep
require_command tail
require_command mktemp
require_command rm

if pgrep -x loraham_daemon >/dev/null; then
  echo "ERROR: loraham_daemon is already running:"
  pgrep -af loraham_daemon
  echo
  echo "Stop it first, then run this script again."
  exit 1
fi

suite_start_seconds=$SECONDS

section "Build daemon"
"$SCRIPT_DIR/build.sh"

section "Build tests"
build_tests

section "Run tests"

tests=(
  "$TEST_DIR/test_data_tx"
  "$TEST_DIR/test_client_output_queue"
  "$TEST_DIR/test_client_nonblocking"
  "$TEST_DIR/test_client_queued_broadcast"
  "$TEST_DIR/test_client_slow_output"
  "$TEST_DIR/test_event_loop_output_flush"
  "$TEST_DIR/test_tx_result"
  "$TEST_DIR/test_radio_health"
  "$TEST_DIR/test_rf_packet"
  "$TEST_DIR/test_event_loop"
  "$TEST_DIR/test_daemon_timing"
  "$TEST_DIR/test_daemon_lifecycle"
  "$TEST_DIR/test_unix_socket"
  "$TEST_DIR/test_config_parser"
  "$TEST_DIR/test_config_stream_buffer"
  "$TEST_DIR/test_config_value"
  "$TEST_DIR/test_config_policy"
  "$TEST_DIR/test_config_validate"
  "$TEST_DIR/test_config_apply_transactional"
  "$TEST_DIR/test_config_dispatch"
  "$TEST_DIR/test_interface_baseline"
  "$TEST_DIR/test_config_stream"
  "$TEST_DIR/test_rssi_multiclient"
  "$TEST_DIR/test_client_lifecycle"
)

overall_rc=0
total_tests=0
passed_tests=0
failed_tests=0
parsed_summaries=0
missing_summaries=0
assert_ok_total=0
assert_fail_total=0
assert_skip_total=0
assert_xfail_total=0
assert_xpass_total=0
results=()
running_tests=false

summary_ok="-"
summary_fail="-"
summary_skip="-"
summary_xfail="-"
summary_xpass="-"
summary_found=0

parse_test_summary() {
  local output_file="$1"
  local summary_line=""

  summary_ok="-"
  summary_fail="-"
  summary_skip="-"
  summary_xfail="-"
  summary_xpass="-"
  summary_found=0

  summary_line="$(grep -E '^Summary:' "$output_file" | tail -1 || true)"

  if [[ -z "$summary_line" ]]; then
    return
  fi

  summary_found=1

  if [[ "$summary_line" =~ ok=([0-9]+) ]]; then
    summary_ok="${BASH_REMATCH[1]}"
  else
    summary_ok=0
  fi

  if [[ "$summary_line" =~ fail=([0-9]+) ]]; then
    summary_fail="${BASH_REMATCH[1]}"
  else
    summary_fail=0
  fi

  if [[ "$summary_line" =~ skip=([0-9]+) ]]; then
    summary_skip="${BASH_REMATCH[1]}"
  else
    summary_skip=0
  fi

  if [[ "$summary_line" =~ xfail=([0-9]+) ]]; then
    summary_xfail="${BASH_REMATCH[1]}"
  else
    summary_xfail=0
  fi

  if [[ "$summary_line" =~ xpass=([0-9]+) ]]; then
    summary_xpass="${BASH_REMATCH[1]}"
  else
    summary_xpass=0
  fi
}

record_test_result() {
  local status="$1"
  local test_name="$2"
  local duration="$3"
  local reason="${4:-}"
  local status_label=""
  local line

  case "$status" in
    ok)
      passed_tests=$((passed_tests + 1))
      status_label="OK"
      ;;
    fail)
      failed_tests=$((failed_tests + 1))
      overall_rc=1
      status_label="FAIL"
      ;;
    *)
      failed_tests=$((failed_tests + 1))
      overall_rc=1
      status_label="FAIL"
      reason="internal-invalid-status"
      ;;
  esac

  if [[ "$summary_found" -eq 1 ]]; then
    parsed_summaries=$((parsed_summaries + 1))
    assert_ok_total=$((assert_ok_total + summary_ok))
    assert_fail_total=$((assert_fail_total + summary_fail))
    assert_skip_total=$((assert_skip_total + summary_skip))
    assert_xfail_total=$((assert_xfail_total + summary_xfail))
    assert_xpass_total=$((assert_xpass_total + summary_xpass))
  else
    missing_summaries=$((missing_summaries + 1))
    if [[ -n "$reason" ]]; then
      reason="$reason no-summary"
    else
      reason="no-summary"
    fi
  fi

  printf -v line "%-5s %-36s %5s %5s %5s %5s %5s %5ss" \
    "$status_label" \
    "$test_name" \
    "$summary_ok" "$summary_fail" "$summary_skip" "$summary_xfail" "$summary_xpass" \
    "$duration"

  if [[ -n "$reason" ]]; then
    line="$line  $reason"
  fi

  results+=("$line")
}

cleanup_lingering_daemon() {
  local test_bin="$1"

  if pgrep -x loraham_daemon >/dev/null; then
    echo "ERROR: loraham_daemon is still running after $test_bin"
    pgrep -af loraham_daemon || true
    pkill -TERM -x loraham_daemon 2>/dev/null || true
    sleep 1
    pkill -KILL -x loraham_daemon 2>/dev/null || true
    return 1
  fi

  return 0
}

cleanup_on_exit() {
  local rc=$?

  if [[ "$running_tests" == true ]] && pgrep -x loraham_daemon >/dev/null; then
    echo
    echo "ERROR: cleaning up lingering loraham_daemon before exit"
    pgrep -af loraham_daemon || true
    pkill -TERM -x loraham_daemon 2>/dev/null || true
    sleep 1
    pkill -KILL -x loraham_daemon 2>/dev/null || true
  fi

  return "$rc"
}

trap cleanup_on_exit EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

run_one_test() {
  local test_bin="$1"
  local test_name
  local rc=0
  local reason=""
  local duration
  local test_start
  local cmd
  local output_file

  test_name="$(basename "$test_bin")"
  total_tests=$((total_tests + 1))

  echo
  echo "================================================================"
  echo "Running: $test_bin"
  echo "================================================================"

  test_start=$SECONDS

  summary_ok="-"
  summary_fail="-"
  summary_skip="-"
  summary_xfail="-"
  summary_xpass="-"
  summary_found=0

  if [[ ! -x "$test_bin" ]]; then
    duration=$((SECONDS - test_start))
    record_test_result fail "$test_name" "$duration" "missing-or-not-executable"
    return
  fi

  output_file="$(mktemp "${TMPDIR:-/tmp}/loraham-test-${test_name}.XXXXXX")"

  cmd=("$test_bin" --bin "$DAEMON_BIN")

  if [[ "$test_bin" == "$TEST_DIR/test_interface_baseline" && "$tx_tests" == true ]]; then
    cmd+=(--rf-tx --rx-seconds "$rx_seconds")
  fi

  if "${cmd[@]}" >"$output_file" 2>&1; then
    rc=0
  else
    rc=$?
    reason="rc=$rc"
  fi

  cat "$output_file"
  parse_test_summary "$output_file"
  rm -f "$output_file"

  sleep 1

  if ! cleanup_lingering_daemon "$test_bin"; then
    rc=1
    if [[ -n "$reason" ]]; then
      reason="$reason daemon-still-running"
    else
      reason="daemon-still-running"
    fi
  fi

  duration=$((SECONDS - test_start))

  if [[ "$rc" -eq 0 ]]; then
    record_test_result ok "$test_name" "$duration"
  else
    record_test_result fail "$test_name" "$duration" "$reason"
  fi
}

running_tests=true

for test_bin in "${tests[@]}"; do
  run_one_test "$test_bin"
done

running_tests=false

suite_elapsed=$((SECONDS - suite_start_seconds))

echo
echo "================================================================"
echo "Final test summary"
echo "================================================================"
printf "%-5s %-36s %5s %5s %5s %5s %5s %5s  %s\n" \
  "STAT" "TEST" "OK" "FAIL" "SKIP" "XFAIL" "XPASS" "TIME" "DETAIL"
for result in "${results[@]}"; do
  echo "$result"
done

echo
echo "Test binaries: $total_tests"
echo "Passed:        $passed_tests"
echo "Failed:        $failed_tests"
echo "Summaries:     $parsed_summaries parsed, $missing_summaries missing"
echo
echo "Assertions:"
echo "OK:            $assert_ok_total"
echo "FAIL:          $assert_fail_total"
echo "SKIP:          $assert_skip_total"
echo "XFAIL:         $assert_xfail_total"
echo "XPASS:         $assert_xpass_total"
echo "Elapsed:       ${suite_elapsed}s"

if [[ "$overall_rc" -eq 0 ]]; then
  echo "OVERALL OK"
else
  echo "OVERALL FAIL"
fi

exit "$overall_rc"
