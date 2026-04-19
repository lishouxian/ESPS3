# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this project is

Firmware + host-side tooling for a **Waveshare ESP32-S3-RLCD-4.2** (4.2" reflective LCD,
400×300, 1-bit, memory-in-pixel, always-on). The screen renders a desk-ambient dashboard
of Mac system state (from `mole status --json`) plus Claude Code session state
(Context % / 5h rate-limit % / 7d rate-limit %). Design direction is deliberately
"Ink on Paper" — see `docs/06-ink-on-paper.md`.

## Architecture you need to hold in your head

Three components that talk across a single USB-CDC port:

```
macOS host                    USB CDC (115200)           ESP32-S3
──────────                    ────────────────           ────────
mole status --json  ┐
                    ├─► mole-bridge.py ──── JSON line ──► consume_line()
claude statusline ──┘    (every 2s)         per frame     render_all()
  (cached JSON)                                           ST7305 framebuffer
                                              ◄── SHOT ── shot.py pulls raw
                                                          15000 bytes → PNG
```

- `~/.claude/statusline.sh` must be patched so every Claude Code query writes
  `rate_limits` + `context_window` to `~/.cache/waza-statusline/last.json`. The patch is
  documented in `docs/05-mole-dashboard.md` and applied automatically by
  `tools/install.sh`.
- Same USB-CDC port is shared by: bridge (normal operation), `shot.py` (framebuffer read),
  and `pio run -t upload` (flashing). Only one can hold the port at a time — this is the
  source of every serial-related bug.

## Essential commands

```bash
# Build + flash firmware (dev machine with PlatformIO)
pio run                          # compile only
pio run -t upload                # compile + flash; auto-kills running bridge not guaranteed
pkill -f mole-bridge && pio run -t upload   # safe flash when bridge is running

# Run bridge
tools/install.sh                 # one-time setup on a machine: venv, mole, patch statusline
tools/install.sh --autostart     # also install launchd agent; bridge runs on login
tools/install.sh --uninstall     # tear it all down
esps3-bridge                     # foreground (after install.sh)
esps3-bridge --port /dev/cu.usbmodem1101 --verbose   # force port, show frames

# Screenshot the live board (CRITICAL — see "UI iteration loop" below)
esps3-shot                       # → /tmp/esps3-shot.png
esps3-shot --out ./snap.png

# launchd management (when --autostart is active)
launchctl unload ~/Library/LaunchAgents/com.esps3.bridge.plist   # stop (needed to flash)
launchctl load ~/Library/LaunchAgents/com.esps3.bridge.plist     # start
launchctl kickstart -k gui/$(id -u)/com.esps3.bridge             # quick restart
tail -f ~/.esps3/log/bridge.{out,err}
```

No test suite — this is a hardware project. Verification is visual via `esps3-shot`.

## UI iteration loop (how you work without the user photographing the board)

**Do not ask the user to take photos.** The board's framebuffer is readable over serial.
After any firmware change:

```bash
pkill -f mole-bridge 2>/dev/null      # free the port
pio run -t upload                     # ~5s
# warm up the bridge briefly so the board has real data (not a blank splash)
/path/to/python3 tools/mole-bridge.py --interval 2 >/dev/null 2>&1 &
disown; sleep 6
tools/shot.sh /tmp/esps3-shot.png     # captures real pixels → PNG
# Then Read the PNG with the Read tool to inspect it yourself.
```

`tools/shot.sh` already handles the bridge stop/restart and picks a Python with
`pyserial+Pillow`. It understands launchd-managed bridges too. **Always inspect the
resulting PNG yourself before asking the user "how does it look?"** — you have
pixel-level visibility.

## Host ↔ board JSON protocol

Bridge sends one `\n`-terminated JSON per frame (every ~2 s). Shape is fixed in
`tools/mole-bridge.py::build_frame` and `src/main.cpp::apply_snapshot`. Schema:

```json
{
  "host":   "macmini-2",
  "hw":     "Mac mini · Apple M4 · 32.0GB",
  "up":     "8d 12h",
  "health": 93,
  "cpu":   {"pct": 18, "p": 4, "e": 6, "load": [n,n,n]},
  "mem":   {"pct": 64, "used_gb": 20.4, "total_gb": 32.0},
  "net":   {"rx": 0.17, "tx": 0.15, "if": "en1", "spark": [0..63]},
  "disk":  {"pct": 64, "used_gb": 146, "total_gb": 228},
  "claude":{"ctx": n|null, "h5": n|null, "h5_r": "3h15m", "d7": n|null, "d7_r": "4d19h"}
}
```

Extra fields are accepted (`net.spark`, `claude.*_r`) but the current firmware ignores
some — that's fine, don't strip them from the bridge. When adding a new field: edit
`build_frame` on the host side, then `apply_snapshot` + a draw function on the board.

Board also understands the bare text line `SHOT\n` — triggers a binary framebuffer dump
used by `shot.py`.

## Design direction (Ink on Paper) — do not drift

The visual design was explicitly chosen over Braun and Terminal alternatives (see
`mockups/design-directions.html`). Rules:

- **No horizontal rules anywhere.** Structure comes from type hierarchy + whitespace.
  If you're tempted to add a divider, bump a label font instead.
- **Serif numerals** (`u8g2_font_ncenB24_tn` / `ncenB14_tr` / `ncenB18_tr`) for data,
  **Helvetica Bold** for all-caps labels (`helvB12_tf`), no other families.
  Bitmap u8g2 ncen family tops out at 24 px for numbers — don't try to go bigger
  without switching families (would break the serif look).
- **1-bit B/W only.** No "grey", no transparency. `Adafruit_GFX` color arg: 0 = white
  paper, 1 = black ink. The ST7305 framebuffer bit convention is the opposite:
  bit=1 means white (pixel not driven — reflective default). `shot.py` handles this
  flip; if you touch either, keep them consistent.
- **Rejected elements (do not resurrect without asking)**: masthead title + HEALTH
  badge, NETWORK section, Top Processes, hero meta lines (`load AVG`, `GB`),
  sparklines, DISK progress bar. See `docs/06-ink-on-paper.md` for rationale.

## Firmware layout (src/)

- `main.cpp` — renders `Snapshot` struct; owns the Y-coordinate constants, the draw
  functions, and the `consume_line` dispatcher (JSON → `apply_snapshot` → `render_all`
  or `SHOT` → `handle_shot`).
- `display_bsp.{h,cpp}` — ST7305 SPI driver. Landscape 1-bit pixel addressing is
  non-trivial (`x/2 * h/4 + invY/4` with bit packing) — the same math lives in
  `shot.py::decode_st7305_landscape`, keep them in sync.
- `rlcd_gfx.h` — thin `Adafruit_GFX` adapter that forwards `drawPixel` to
  `DisplayPort::RLCD_SetPixel`. `U8g2_for_Adafruit_GFX` attaches on top of this for
  text/UTF-8; raw shapes go through Adafruit_GFX directly.

Pins are declared at the top of `main.cpp` and match `docs/01-hardware.md`. The only
ones we drive today are the LCD SPI set (MOSI 12, SCK 11, DC 5, CS 40, RST 41).

## Known gotchas

- **Bridge must be killed before `pio run -t upload`**. If launchd is running it,
  `launchctl unload` the plist first, or flashing will fail with a busy port.
  `tools/shot.sh` has this handled but the build flow doesn't.
- **launchd PATH must include `/usr/sbin` and `/sbin`** — mole calls `system_profiler`
  and `sysctl` from those. `install.sh`'s generated plist gets this right; anyone
  hand-editing it will see `hw.model` and `up` go blank.
- **Network access in China**: brew / pio / git push all route through `http://127.0.0.1:7890`
  (the user's `f1` function in `~/.zshrc` exports the proxy envs). Long-running
  commands should set `http_proxy`, `https_proxy`, `ALL_PROXY` explicitly — a fresh
  subshell doesn't inherit them unless you export first.
- **Font choice affects memory**. Each u8g2 font is 2–10 KB in flash. Don't add fonts
  casually; reuse the existing set (helvB08/10/12, ncenR10/12, ncenB14/18/24_tn).
- **The bridge sends UTF-8** (`·` = U+00B7). U8g2 `_tf` variants include this glyph;
  `_tr` variants don't. If a label renders as `?` or garbage, check the font suffix.

## Documentation layout

The `docs/` files are numbered chronologically, and each one is worth reading for
specific tasks:

- `01-hardware.md` — pin map, IC list, datasheets
- `02-platformio-setup.md`, `03-first-flash.md` — PIO + first-light history
- `04-display.md` — ST7305 driver derivation (kept for reference)
- `05-mole-dashboard.md` — the current architecture + JSON protocol
- `06-ink-on-paper.md` — design rationale + what was rejected
- `07-shot-tool.md` — how framebuffer readback works
- `08-deployment.md` — installer, launchd, port auto-discovery
