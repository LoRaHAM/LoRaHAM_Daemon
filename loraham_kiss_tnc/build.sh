#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd -- "$script_dir/.." && pwd -P)"
component="$(basename "$script_dir")"
default_out="$script_dir/$component"

usage() {
  cat <<'EOF'
Usage: build.sh [OUTPUT]
       build.sh --clean [OUTPUT]
       build.sh --help

Build the KISS/TCP TNC bridge.

Environment:
  CC             C compiler, default: gcc
  CFLAGS         Full C flags override
  CFLAGS_EXTRA   Extra C flags appended to defaults
  LDFLAGS        Link flags
  LDFLAGS_EXTRA  Extra link flags appended
EOF
}

fail() {
  echo "[ERR] $*" >&2
  exit 1
}

mode="build"
out="$default_out"

case "${1:-}" in
  -h|--help)
    usage
    exit 0
    ;;
  --clean)
    mode="clean"
    shift
    out="${1:-$out}"
    ;;
  "")
    ;;
  -*)
    fail "unknown option: $1"
    ;;
  *)
    out="$1"
    ;;
esac

[[ $# -le 1 ]] || fail "too many arguments"

sources=(
  main.c
  bridge.c
  cli.c
  tcp_server.c
  kiss.c
  ax25.c
  tnc2.c
  loraham_sock.c
  loraham_kiss_tnc.c
  config.c
)

source_paths=()
for source in "${sources[@]}"; do
  path="$script_dir/$source"
  [[ -f "$path" ]] || fail "missing source file: $source"
  source_paths+=("$path")
done

if [[ "$mode" == "clean" ]]; then
  rm -f "$out"
  echo "[OK] Removed $out"
  exit 0
fi

CC="${CC:-gcc}"
default_cflags="-Wall -Wextra -Wpedantic -Werror -std=c11 -I $script_dir"
CFLAGS="${CFLAGS:-$default_cflags}"
CFLAGS="${CFLAGS} ${CFLAGS_EXTRA:-}"
LDFLAGS="${LDFLAGS:-} ${LDFLAGS_EXTRA:-}"

echo "[BUILD] component=$component"
echo "[BUILD] CC=$CC"
echo "[BUILD] OUT=$out"

mkdir -p "$(dirname "$out")"

# shellcheck disable=SC2086
"$CC" $CFLAGS \
  -o "$out" \
  "${source_paths[@]}" \
  $LDFLAGS

echo "[OK] Built $out"
