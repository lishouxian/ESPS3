#!/usr/bin/env bash
# Capture a PNG of the live ESPS3 screen.
# Pauses any running bridge (so it releases the serial port), takes the shot,
# then restarts the bridge in the background if there was one.
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
OUT="${1:-/tmp/esps3-shot.png}"

# Pick a Python that has pyserial + Pillow.
# Priority: user override → venv installed by tools/install.sh → pio's venv
#   (only present on dev machines) → system python3 from PATH.
pick_py() {
  for cand in \
    "${ESPS3_PY:-}" \
    "$HOME/.esps3/venv/bin/python3" \
    "/opt/homebrew/Cellar/platformio/6.1.19_1/libexec/bin/python3" \
    "$(command -v python3 || true)"
  do
    [ -n "$cand" ] && [ -x "$cand" ] || continue
    if "$cand" -c 'import serial, PIL' >/dev/null 2>&1; then
      echo "$cand"
      return
    fi
  done
  return 1
}

PY="$(pick_py || true)"
if [ -z "$PY" ]; then
  echo "No python3 with pyserial + Pillow found." >&2
  echo "Run tools/install.sh first, or set ESPS3_PY=/path/to/python3" >&2
  exit 1
fi

# If launchd is managing the bridge, we just kick it and let launchd respawn;
# otherwise we pkill + nohup-restart ourselves.
launchd_managed=0
if launchctl list com.esps3.bridge >/dev/null 2>&1; then
  launchd_managed=1
fi

had_bridge=0
if pgrep -f mole-bridge.py >/dev/null 2>&1; then
  had_bridge=1
  pkill -f mole-bridge.py || true
  sleep 0.8
fi

"$PY" "$DIR/shot.py" --out "$OUT"

if [ "$had_bridge" = 1 ] && [ "$launchd_managed" = 0 ]; then
  nohup "$PY" "$DIR/mole-bridge.py" --interval 2 >/dev/null 2>&1 &
  disown || true
fi
# When launchd is in charge, ThrottleInterval (5s) will respawn the bridge.

echo "$OUT"
