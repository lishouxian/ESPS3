#!/usr/bin/env python3
"""mole-bridge-ble — BLE variant of mole-bridge.

Scans for an ESPS3 board advertising Nordic UART Service under the name
"ESPS3-XXXX" (XXXX = BT MAC tail), connects, and pushes the same newline-
delimited JSON frames as the USB-CDC bridge. Reconnects on drop.

Usage:
    tools/mole-bridge-ble.py                     # auto-find, connect, run
    tools/mole-bridge-ble.py --scan              # list nearby ESPS3 boards
    tools/mole-bridge-ble.py --name ESPS3-AB12   # pin to a specific board

Requires: mole, bleak (pip install bleak).
"""

from __future__ import annotations

import argparse
import asyncio
import json
import signal
import sys
import time

from bleak import BleakClient, BleakScanner
from bleak.backends.device import BLEDevice
from bleak.exc import BleakError

from snapshot import FrameBuilder, mole_snapshot

# Standard Nordic UART Service — matches ble_transport.cpp.
NUS_SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX      = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  # peripheral receives
NUS_TX      = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  # peripheral notifies

NAME_PREFIX = "ESPS3-"
SCAN_TIMEOUT = 10.0
# NUS negotiates a ~247B MTU on macOS; keep payloads well under that so one
# write-without-response is always one ATT packet.
WRITE_CHUNK = 180


async def find_device(exact_name: str | None) -> BLEDevice | None:
    label = exact_name if exact_name else f"{NAME_PREFIX}*"
    print(f"[bridge-ble] scanning for {label} (up to {SCAN_TIMEOUT:.0f}s)...",
          file=sys.stderr)
    devices = await BleakScanner.discover(timeout=SCAN_TIMEOUT)
    for d in devices:
        n = d.name or ""
        if exact_name:
            if n == exact_name:
                return d
        elif n.startswith(NAME_PREFIX):
            return d
    return None


async def scan_all() -> None:
    print(f"[bridge-ble] scanning for {NAME_PREFIX}* ({SCAN_TIMEOUT:.0f}s)...",
          file=sys.stderr)
    devices = await BleakScanner.discover(timeout=SCAN_TIMEOUT)
    hits = [d for d in devices if (d.name or "").startswith(NAME_PREFIX)]
    if not hits:
        print("no ESPS3 boards seen.", file=sys.stderr)
        return
    for d in hits:
        print(f"{d.name:<16}  {d.address}")


async def send_line(client: BleakClient, payload: bytes) -> None:
    for i in range(0, len(payload), WRITE_CHUNK):
        await client.write_gatt_char(
            NUS_RX, payload[i:i + WRITE_CHUNK], response=False)


async def run_session(client: BleakClient, args,
                      stop: asyncio.Event) -> None:
    fb = FrameBuilder()
    last_sent = 0.0

    if args.verbose:
        def on_tx(_sender, data: bytearray):
            try:
                text = bytes(data).decode("utf-8", errors="replace").rstrip()
            except Exception:
                text = repr(data)
            if text:
                sys.stderr.write(f"                     << {text}\n")
        try:
            await client.start_notify(NUS_TX, on_tx)
        except BleakError:
            pass

    while not stop.is_set() and client.is_connected:
        snap = mole_snapshot()
        if snap is not None:
            frame = fb.build(snap)
            payload = (json.dumps(frame, separators=(",", ":")) + "\n").encode()
            try:
                await send_line(client, payload)
            except BleakError as e:
                print(f"[bridge-ble] write failed: {e}", file=sys.stderr)
                return
            if args.verbose:
                sys.stderr.write(
                    f"[{time.strftime('%H:%M:%S')}] "
                    f"tx {len(payload):4d}B  cpu={frame['cpu']['pct']:3d}% "
                    f"mem={frame['mem']['pct']:3d}% "
                    f"net={frame['net']['rx']:.2f}/{frame['net']['tx']:.2f}\n")
            last_sent = time.time()

        # Sleep until next interval, but wake early if stop is requested.
        remaining = max(0.0, args.interval - (time.time() - last_sent))
        if remaining > 0:
            try:
                await asyncio.wait_for(stop.wait(), timeout=remaining)
                return  # stop.set()
            except asyncio.TimeoutError:
                pass


async def main_loop(args, stop: asyncio.Event) -> None:
    while not stop.is_set():
        dev = await find_device(args.name)
        if dev is None:
            print("[bridge-ble] no ESPS3 device found; retry in 3s",
                  file=sys.stderr)
            try:
                await asyncio.wait_for(stop.wait(), timeout=3)
                return
            except asyncio.TimeoutError:
                continue

        print(f"[bridge-ble] connecting to {dev.name}  ({dev.address})",
              file=sys.stderr)
        try:
            async with BleakClient(dev) as client:
                if not client.is_connected:
                    print("[bridge-ble] connect failed", file=sys.stderr)
                    continue
                mtu = getattr(client, "mtu_size", None)
                print(f"[bridge-ble] connected"
                      + (f"; MTU ~{mtu}B" if mtu else ""),
                      file=sys.stderr)
                await run_session(client, args, stop)
        except BleakError as e:
            print(f"[bridge-ble] ble error: {e}; reconnect in 2s",
                  file=sys.stderr)
            try:
                await asyncio.wait_for(stop.wait(), timeout=2)
                return
            except asyncio.TimeoutError:
                pass


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--name", default=None,
                   help="exact advertised name; default scans for ESPS3-*")
    p.add_argument("--interval", type=float, default=2.0)
    p.add_argument("--scan", action="store_true",
                   help="list nearby ESPS3 boards and exit")
    p.add_argument("--verbose", action="store_true")
    args = p.parse_args()

    loop = asyncio.new_event_loop()
    stop = asyncio.Event()

    def _sig(*_):
        loop.call_soon_threadsafe(stop.set)
    signal.signal(signal.SIGINT, _sig)
    signal.signal(signal.SIGTERM, _sig)

    try:
        if args.scan:
            loop.run_until_complete(scan_all())
        else:
            loop.run_until_complete(main_loop(args, stop))
    finally:
        loop.close()
    print("[bridge-ble] bye.", file=sys.stderr)


if __name__ == "__main__":
    main()
