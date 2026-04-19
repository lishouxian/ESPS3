"""Shared data layer for ESPS3 bridges.

Both `mole-bridge.py` (USB-CDC) and `mole-bridge-ble.py` (BLE / NUS) pull
their frame contents from here, so the two transports stay in lockstep.

Public entry points:
  build_frame(mole_json) -> dict     # ready for json.dumps + newline ship
  mole_snapshot()        -> dict|None
  read_claude_cache()    -> dict|None
  ClaudeBridge           # carries running spark state between calls
"""

from __future__ import annotations

import collections
import json
import re
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path


SPARK_N = 48              # must match Snapshot::SPARK_N in src/main.cpp
SCALE_DECAY = 0.985       # sparkline auto-scale decay per sample

CLAUDE_CACHE = Path.home() / ".cache" / "waza-statusline" / "last.json"
CLAUDE_STALE_SECONDS = 6 * 3600  # match statusline.sh CACHE_MAX_AGE


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
        print(f"[snapshot] mole failed: {e}", file=sys.stderr)
        return None


# ── Transform helpers ────────────────────────────────────────────────
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
    days = hours // 24
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


def compact_hw(hw: dict) -> str:
    bits = []
    if hw.get("model"):     bits.append(hw["model"])
    if hw.get("cpu_model"): bits.append(hw["cpu_model"])
    if hw.get("total_ram"): bits.append(hw["total_ram"].replace(" ", ""))
    return " \u00B7 ".join(bits)


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
    for d in disks or []:
        if d.get("mount") == "/":
            return d
    return (disks or [{}])[0]


# ── Frame builder with running state (spark history, scale) ──────────
class FrameBuilder:
    def __init__(self) -> None:
        self.spark: collections.deque = collections.deque(
            [0.0] * SPARK_N, maxlen=SPARK_N)
        self.scale_max: float = 1.0

    def build(self, m: dict) -> dict:
        cpu = m.get("cpu", {})
        mem = m.get("memory", {})
        hw  = m.get("hardware", {})
        net = primary_network(m.get("network", []))
        dsk = primary_disk(m.get("disks", []))

        rx = float(net.get("rx_rate_mbs", 0) or 0)
        tx = float(net.get("tx_rate_mbs", 0) or 0)
        total_mbs = rx + tx

        self.spark.append(total_mbs)
        self.scale_max = max(self.scale_max * SCALE_DECAY, total_mbs, 0.1)
        spark_norm = [int(min(63, round(v / self.scale_max * 63)))
                      for v in self.spark]

        claude = read_claude_cache()

        return {
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
