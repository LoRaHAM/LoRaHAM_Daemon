#!/usr/bin/env bash
set -euo pipefail

# Serial KISS adapter for loraham_kiss_tnc.
# Client sees:        /tmp/loraham_kiss
# loraham_kiss_tnc:   KISS/TCP on 127.0.0.1:8001

LINK="${LINK:-/tmp/loraham_kiss}"
HOST="${HOST:-127.0.0.1}"
PORT="${PORT:-8001}"
MODE="${MODE:-666}"
RESTART_DELAY="${RESTART_DELAY:-2}"
LOCKFILE="${LOCKFILE:-/tmp/loraham_kiss_socat.lock}"

log() {
  printf '%s %s\n' "$(date '+%H:%M:%S')" "$*"
}

cleanup_link() {
  if [ -L "$LINK" ]; then
    rm -f "$LINK"
  fi
}

cleanup() {
  log "[STOP] Cleaning up $LINK"
  cleanup_link
}

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "[ERR] Missing command: $1" >&2
    echo "Install it with: sudo apt install $1" >&2
    exit 1
  fi
}

require_cmd socat

# Avoid accidentally deleting a real file.
if [ -e "$LINK" ] && [ ! -L "$LINK" ]; then
  echo "[ERR] $LINK exists but is not a symlink." >&2
  echo "Remove it manually if it is safe:" >&2
  echo "  rm -f '$LINK'" >&2
  exit 1
fi

# Optional lock, available on normal Linux systems.
if command -v flock >/dev/null 2>&1; then
  exec 9>"$LOCKFILE"
  if ! flock -n 9; then
    echo "[ERR] Another loraham_kiss socat bridge seems to be running." >&2
    exit 1
  fi
fi

trap cleanup EXIT INT TERM

log "[INIT] Xastir serial KISS adapter"
log "[CFG]  LINK=$LINK"
log "[CFG]  TCP=$HOST:$PORT"
log "[INFO] Configure Xastir as Serial KISS TNC on: $LINK"
log "[INFO] Stop with Ctrl+C"

while true; do
  cleanup_link

  log "[START] Creating PTY and connecting to $HOST:$PORT"

  set +e
  socat -d -d \
    "PTY,link=$LINK,raw,echo=0,waitslave,mode=$MODE" \
    "TCP:$HOST:$PORT"
  rc=$?
  set -e

  cleanup_link

  log "[WARN] socat exited with code $rc"
  log "[INFO] Restarting in ${RESTART_DELAY}s..."
  sleep "$RESTART_DELAY"
done
