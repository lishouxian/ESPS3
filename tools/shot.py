#!/usr/bin/env python3
"""Capture a PNG of exactly what's drawn on the ESPS3 screen.

Sends 'SHOT\\n' to the board; the board replies with
    SHOT-BEGIN <fb_bytes> <w> <h>
    <fb_bytes raw bytes>
    \\n
    SHOT-END\\n
Decodes the ST7305 landscape 1-bit packing back to a PIL image.

Usage:
    tools/shot.py [--port /dev/cu.usbmodem1101] [--out /tmp/esps3-shot.png]
"""
from __future__ import annotations

import argparse
import struct
import sys
import time
from pathlib import Path

import serial        # pyserial
from serial.tools import list_ports
from PIL import Image

ESPRESSIF_VID = 0x303A

def find_esps3_port() -> str | None:
    for p in list_ports.comports():
        if p.vid == ESPRESSIF_VID:
            return p.device
    return None


def wait_begin(s: serial.Serial, deadline: float):
    """Read line-by-line until we see 'SHOT-BEGIN <n> <w> <h>'."""
    buf = bytearray()
    while time.time() < deadline:
        b = s.read(1)
        if not b:
            continue
        buf += b
        if b == b"\n":
            line = bytes(buf).rstrip(b"\r\n")
            buf.clear()
            if line.startswith(b"SHOT-BEGIN"):
                parts = line.split()
                if len(parts) >= 4:
                    return int(parts[1]), int(parts[2]), int(parts[3])
            # else: ignore stray log lines
    raise TimeoutError("no SHOT-BEGIN from board within timeout")


def read_exact(s: serial.Serial, n: int, deadline: float) -> bytes:
    out = bytearray()
    while len(out) < n and time.time() < deadline:
        chunk = s.read(n - len(out))
        if chunk:
            out += chunk
    if len(out) < n:
        raise TimeoutError(f"only read {len(out)}/{n} framebuffer bytes")
    return bytes(out)


def decode_st7305_landscape(raw: bytes, w: int, h: int) -> Image.Image:
    """Invert the ST7305 pixel-addressing used by src/display_bsp.cpp.
    In that buffer, bit=1 means "white paper" (ColorWhite=0xFF) and
    bit=0 means "black ink" (ColorBlack=0x00)."""
    img = Image.new("1", (w, h), 1)  # start as white paper
    px  = img.load()
    h4  = h // 4
    for x in range(w):
        byte_x_base = x >> 1
        local_x     = x & 1
        for y in range(h):
            inv_y   = h - 1 - y
            block_y = inv_y >> 2
            local_y = inv_y & 3
            index   = byte_x_base * h4 + block_y
            bit     = 7 - ((local_y << 1) | local_x)
            if not (raw[index] & (1 << bit)):
                px[x, y] = 0  # black ink where the buffer bit is 0
    return img


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=None,
                    help="serial port (auto-detected if omitted)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--out",  default="/tmp/esps3-shot.png")
    ap.add_argument("--timeout", type=float, default=5.0)
    args = ap.parse_args()

    port = args.port or find_esps3_port()
    if port is None:
        print("No ESP32-S3 serial port found.", file=sys.stderr)
        sys.exit(1)

    s = serial.Serial()
    s.port = port
    s.baudrate = args.baud
    s.timeout = 0.2
    s.dtr = False
    s.rts = False
    s.open()
    time.sleep(0.2)
    s.reset_input_buffer()

    s.write(b"SHOT\n")
    s.flush()

    deadline = time.time() + args.timeout
    n, w, h = wait_begin(s, deadline)
    print(f"[shot] receiving {n} bytes for {w}x{h}", file=sys.stderr)

    raw = read_exact(s, n, deadline + 3.0)
    # Drain until SHOT-END or timeout (we don't strictly need it)
    end_deadline = time.time() + 1.5
    while time.time() < end_deadline:
        line = s.readline()
        if line.rstrip() == b"SHOT-END":
            break

    s.close()

    img = decode_st7305_landscape(raw, w, h)
    out = Path(args.out)
    img.save(out)
    print(str(out))

if __name__ == "__main__":
    main()
