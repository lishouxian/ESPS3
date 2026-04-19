#!/usr/bin/env python3
"""mole-bridge — pipe `mole status --json` to the ESPS3 display over USB-CDC.

Usage:
    tools/mole-bridge.py [--port /dev/cu.usbmodem1101] [--interval 2]

Requires: mole (https://github.com/tw93/Mole), pyserial (pip install pyserial).

Frame construction lives in snapshot.py so the BLE bridge stays in lockstep.
"""

from __future__ import annotations

import argparse
import json
import signal
import sys
import time

import serial  # pyserial
from serial.tools import list_ports

from snapshot import FrameBuilder, mole_snapshot

# Espressif Systems USB VID — matches ESP32-S3 native USB CDC.
ESPRESSIF_VID = 0x303A

running = True


def _sigint(_sig, _frm):
    global running
    running = False


signal.signal(signal.SIGINT, _sigint)
signal.signal(signal.SIGTERM, _sigint)


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


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--port", default=None,
                   help="serial port (auto-detected if omitted)")
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--interval", type=float, default=2.0)
    p.add_argument("--verbose", action="store_true")
    args = p.parse_args()

    fb = FrameBuilder()
    ser: serial.Serial | None = None
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

            frame = fb.build(snap)
            line = json.dumps(frame, separators=(",", ":")) + "\n"
            ser.write(line.encode())
            ser.flush()
            back = ser.read(2048)
            if args.verbose:
                sys.stderr.write(
                    f"[{time.strftime('%H:%M:%S')}] "
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
                if ser:
                    ser.close()
            except Exception:
                pass
            ser = None
            time.sleep(2.0)
            continue

        remaining = args.interval - (time.time() - last_sent)
        if remaining > 0:
            time.sleep(remaining)

    if ser and ser.is_open:
        ser.close()
    print("[mole-bridge] bye.", file=sys.stderr)


if __name__ == "__main__":
    main()
