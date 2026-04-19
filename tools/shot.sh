#!/usr/bin/env bash
# Capture a PNG of the live ESPS3 screen.
# Pauses the bridge (so it releases the serial port), takes the shot,
# then restarts the bridge in the background.
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
PY="/opt/homebrew/Cellar/platformio/6.1.19_1/libexec/bin/python3"
OUT="${1:-/tmp/esps3-shot.png}"

had_bridge=0
if pgrep -f mole-bridge.py >/dev/null 2>&1; then
  had_bridge=1
  pkill -f mole-bridge.py || true
  sleep 0.8
fi

"$PY" "$DIR/shot.py" --out "$OUT"

if [ "$had_bridge" = 1 ]; then
  nohup "$PY" "$DIR/mole-bridge.py" --interval 2 >/dev/null 2>&1 &
  disown || true
fi

echo "$OUT"
