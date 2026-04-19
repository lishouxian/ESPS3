#!/usr/bin/env python3
"""mole-bridge — pipe `mole status --json` to the ESPS3 display over USB-CDC.

Usage:
    tools/mole-bridge.py [--port /dev/cu.usbmodem1101] [--interval 2]

Requires: mole (https://github.com/tw93/Mole), pyserial (pip install pyserial).
"""

from __future__ import annotations

import argparse
import collections
import json
import math
import os
import re
import signal
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

import serial  # pyserial
from serial.tools import list_ports

# Espressif Systems USB VID — matches ESP32-S3 native USB CDC.
ESPRESSIF_VID = 0x303A

# ── Config ────────────────────────────────────────────────────────────
SPARK_N = 48      # must match Snapshot::SPARK_N in src/main.cpp
SCALE_DECAY = 0.985  # sparkline auto-scale decay per sample

CLAUDE_CACHE = Path.home() / ".cache" / "waza-statusline" / "last.json"
CLAUDE_STALE_SECONDS = 6 * 3600  # match statusline.sh CACHE_MAX_AGE

# ── State ─────────────────────────────────────────────────────────────
spark = collections.deque([0.0] * SPARK_N, maxlen=SPARK_N)
scale_max = 1.0   # running max for sparkline normalization (MB/s)
running = True

def _sigint(_sig, _frm):
    global running
    running = False
signal.signal(signal.SIGINT, _sigint)
signal.signal(signal.SIGTERM, _sigint)

# ── mole invocation ───────────────────────────────────────────────────
def mole_snapshot() -> dict | None:
    try:
        out = subprocess.check_output(
            ["mole", "status", "--json"], timeout=5,
            stderr=subprocess.DEVNULL,
        )
        return json.loads(out)
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired,
            json.JSONDecodeError, FileNotFoundError) as e:
        print(f"[mole-bridge] mole failed: {e}", file=sys.stderr)
        return None

# ── Transform: mole JSON -> board schema ─────────────────────────────
def shorten_host(h: str) -> str:
    return re.sub(r"\.local$", "", h or "")

def format_remaining(resets_at) -> str:
    """Turn an ISO timestamp / epoch seconds into 'XhYm' / 'XdYh'."""
    if resets_at is None or resets_at == "":
        return ""
    try:
        if isinstance(resets_at, (int, float)):
            epoch = float(resets_at)
        else:
            s = str(resets_at)
            # trailing Z normalization
            if s.endswith("Z"):
                s = s[:-1] + "+00:00"
            epoch = datetime.fromisoformat(s).timestamp()
    except (ValueError, TypeError):
        return ""
    diff = int(epoch - time.time())
    if diff <= 0:
        return ""
    mins = diff // 60
    hours = mins // 60
    days  = hours // 24
    if days >= 1:
        return f"{days}d{hours % 24}h"
    if hours >= 1:
        return f"{hours}h{mins % 60}m"
    return f"{mins}m"

def read_claude_cache() -> dict | None:
    """Pull Context + 5h/7d usage from ~/.cache/waza-statusline/last.json."""
    try:
        st = CLAUDE_CACHE.stat()
    except FileNotFoundError:
        return None
    if time.time() - st.st_mtime > CLAUDE_STALE_SECONDS:
        return {"stale": True}
    try:
        with CLAUDE_CACHE.open() as f:
            d = json.load(f)
    except (json.JSONDecodeError, OSError):
        return None

    ctx = d.get("context_window") or {}
    cur = ctx.get("current_usage") or {}
    used = (cur.get("input_tokens") or 0) \
         + (cur.get("cache_creation_input_tokens") or 0) \
         + (cur.get("cache_read_input_tokens") or 0)
    total = ctx.get("context_window_size") or 0
    ctx_pct = int(round(used / total * 100)) if total else None

    rl = d.get("rate_limits") or {}
    f5 = rl.get("five_hour") or {}
    f7 = rl.get("seven_day") or {}

    def _pct(o):
        p = o.get("used_percentage")
        return int(round(p)) if p is not None else None

    return {
        "stale": False,
        "ctx":  ctx_pct,
        "h5":   _pct(f5),
        "h5_r": format_remaining(f5.get("resets_at")),
        "d7":   _pct(f7),
        "d7_r": format_remaining(f7.get("resets_at")),
    }

def short_name(n: str, lim: int = 12) -> str:
    """Trim process names; board-side converter handles UTF-8 fallbacks."""
    n = (n or "").strip()
    return n if len(n) <= lim else n[: lim - 2] + ".."

def compact_hw(hw: dict) -> str:
    bits = []
    if hw.get("model"):     bits.append(hw["model"])
    if hw.get("cpu_model"): bits.append(hw["cpu_model"])
    if hw.get("total_ram"): bits.append(hw["total_ram"].replace(" ", ""))
    return " · ".join(bits)

def primary_network(nets: list[dict]) -> dict:
    """First interface that has an IP and non-zero activity; else first-with-IP."""
    for n in nets or []:
        if n.get("ip") and (n.get("rx_rate_mbs", 0) or n.get("tx_rate_mbs", 0)):
            return n
    for n in nets or []:
        if n.get("ip"):
            return n
    return (nets or [{}])[0]

def primary_disk(disks: list[dict]) -> dict:
    """Root mount."""
    for d in disks or []:
        if d.get("mount") == "/":
            return d
    return (disks or [{}])[0]

def build_frame(m: dict) -> dict:
    global scale_max

    cpu = m.get("cpu", {})
    mem = m.get("memory", {})
    hw  = m.get("hardware", {})
    net = primary_network(m.get("network", []))
    dsk = primary_disk(m.get("disks", []))
    procs = m.get("top_processes") or []

    rx = float(net.get("rx_rate_mbs", 0) or 0)
    tx = float(net.get("tx_rate_mbs", 0) or 0)
    total_mbs = rx + tx

    spark.append(total_mbs)
    scale_max = max(scale_max * SCALE_DECAY, total_mbs, 0.1)
    spark_norm = [int(min(63, round(v / scale_max * 63))) for v in spark]

    claude = read_claude_cache()

    frame = {
        "host":   shorten_host(m.get("host", "")),
        "hw":     compact_hw(hw),
        "up":     m.get("uptime", ""),
        "health": int(m.get("health_score", -1)),
        "cpu": {
            "pct":  int(round(cpu.get("usage", 0))),
            "p":    int(cpu.get("p_core_count", 0)),
            "e":    int(cpu.get("e_core_count", 0)),
            "load": [
                round(cpu.get("load1", 0), 2),
                round(cpu.get("load5", 0), 2),
                round(cpu.get("load15", 0), 2),
            ],
        },
        "mem": {
            "pct":      int(round(mem.get("used_percent", 0))),
            "used_gb":  round(mem.get("used", 0) / 1024**3, 1),
            "total_gb": round(mem.get("total", 0) / 1024**3, 1),
        },
        "net": {
            "rx":    round(rx, 2),
            "tx":    round(tx, 2),
            "if":    net.get("name", ""),
            "spark": spark_norm,
        },
        "disk": {
            "pct":      int(round(dsk.get("used_percent", 0))),
            "used_gb":  round(dsk.get("used", 0) / 1024**3, 1),
            "total_gb": round(dsk.get("total", 0) / 1024**3, 1),
        },
        "claude": {
            "ctx":  claude["ctx"]  if claude and not claude.get("stale") else None,
            "h5":   claude["h5"]   if claude and not claude.get("stale") else None,
            "h5_r": (claude or {}).get("h5_r", "") if claude and not claude.get("stale") else "",
            "d7":   claude["d7"]   if claude and not claude.get("stale") else None,
            "d7_r": (claude or {}).get("d7_r", "") if claude and not claude.get("stale") else "",
        },
    }
    return frame

# ── Serial output ─────────────────────────────────────────────────────
def find_esps3_port() -> str | None:
    """Pick first serial port that matches the ESP32-S3 Espressif VID."""
    for p in list_ports.comports():
        if p.vid == ESPRESSIF_VID:
            return p.device
    return None

def open_serial(port: str, baud: int) -> serial.Serial:
    s = serial.Serial()
    s.port = port
    s.baudrate = baud
    s.timeout = 0
    s.write_timeout = 2
    # Do NOT assert DTR/RTS on open — on ESP32-S3 native USB CDC, the
    # Arduino core treats certain DTR/RTS combinations as reset signals.
    s.dtr = False
    s.rts = False
    s.open()
    return s

# ── Main loop ─────────────────────────────────────────────────────────
def main():
    p = argparse.ArgumentParser()
    p.add_argument("--port", default=None,
                   help="serial port (auto-detected if omitted)")
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--interval", type=float, default=2.0)
    p.add_argument("--verbose", action="store_true")
    args = p.parse_args()

    ser = None
    last_sent = 0.0
    while running:
        try:
            if ser is None or not ser.is_open:
                port = args.port or find_esps3_port()
                if port is None:
                    print("[mole-bridge] no ESP32-S3 serial port found; "
                          "retrying in 3s", file=sys.stderr)
                    time.sleep(3)
                    continue
                print(f"[mole-bridge] opening {port} @ {args.baud}",
                      file=sys.stderr)
                ser = open_serial(port, args.baud)

            snap = mole_snapshot()
            if snap is None:
                time.sleep(args.interval)
                continue

            frame = build_frame(snap)
            line = json.dumps(frame, separators=(",", ":")) + "\n"
            ser.write(line.encode())
            ser.flush()
            # Drain anything the board says back (parse errors, rx confirms)
            back = ser.read(2048)
            if args.verbose:
                sys.stderr.write(f"[{time.strftime('%H:%M:%S')}] "
                                 f"tx {len(line):4d}B  cpu={frame['cpu']['pct']:3d}% "
                                 f"mem={frame['mem']['pct']:3d}% "
                                 f"net={frame['net']['rx']:.2f}/{frame['net']['tx']:.2f}\n")
                if back:
                    for ln in back.decode('utf-8', errors='replace').splitlines():
                        if ln.strip():
                            sys.stderr.write(f"                     << {ln}\n")
            last_sent = time.time()

        except (serial.SerialException, OSError) as e:
            print(f"[mole-bridge] serial error: {e}; retrying in 2s",
                  file=sys.stderr)
            try:
                if ser: ser.close()
            except Exception:
                pass
            ser = None
            time.sleep(2.0)
            continue

        # sleep remainder of interval
        remaining = args.interval - (time.time() - last_sent)
        if remaining > 0:
            time.sleep(remaining)

    if ser and ser.is_open:
        ser.close()
    print("[mole-bridge] bye.", file=sys.stderr)

if __name__ == "__main__":
    main()
