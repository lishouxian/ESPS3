# Mac 系统面板（mole 桥）

把 Mac 的实时系统状态 + Claude Code 会话状态挂到这块 4.2″ 反射屏上。
Mac mini（带 `mole`）运行一个 Python 桥，每 2 秒把整合后的数据以一行 JSON 通过 USB-CDC
推给 ESP32-S3，板子解析并渲染。

## 总体架构

```
┌─────────────────────────┐          ┌──────────┐          ┌──────────┐
│  macOS host             │          │ USB CDC  │          │ ESP32-S3 │
│                         │          │          │          │          │
│  mole status --json  ◀──┼──────────┼──────────┼──────────┤          │
│  claude statusline   ◀──┼─ cache ─▶│          │          │          │
│          │              │          │          │          │          │
│  tools/mole-bridge.py ──┼─JSON line┼───115200─┼─────────▶│  ST7305  │
│          每 2 s          │          │          │          │  400×300 │
└─────────────────────────┘          └──────────┘          └──────────┘
```

- **Mac 端**：
  - `mole status --json` —— 系统状态（CPU/RAM/disk/net/温度/进程/uptime…）
  - `~/.claude/statusline.sh` 每次 Claude Code 跑 status line 时，把 `context_window` +
    `rate_limits` 缓存到 `~/.cache/waza-statusline/last.json`
  - `tools/mole-bridge.py` —— 合并这两份数据，筛出板子要用的字段，压成一行 JSON 发给板子
- **板子**：收到 JSON → 填 `Snapshot` 结构 → 重绘整屏。用 U8g2_for_Adafruit_GFX 的衬线字体
  （"Ink on Paper" 设计方向，详见 [06-ink-on-paper.md](06-ink-on-paper.md)）。

## Host → Board 协议

每 2 秒一行 `\n` 终止的紧凑 JSON，板子按行切片、ArduinoJson 解析，出错就 `Serial.printf`
一条 "JSON parse error" 日志（用 `tools/shot.py` 或裸 pyserial 可捕获）。

```json
{
  "host":   "macmini-2",
  "hw":     "Mac mini · Apple M4 · 32.0GB",
  "up":     "8d 12h",
  "health": 93,
  "cpu":   {"pct": 18, "p": 4, "e": 6, "load": [2.93, 2.15, 1.87]},
  "mem":   {"pct": 64, "used_gb": 20.4, "total_gb": 32.0},
  "net":   {"rx": 0.17, "tx": 0.15, "if": "en1", "spark": [...]},
  "disk":  {"pct": 64, "used_gb": 146, "total_gb": 228},
  "claude":{
    "ctx":  33, "h5": 35, "h5_r": "3h15m",
                 "d7": 7,  "d7_r": "4d19h"
  }
}
```

其中 `claude` 字段可能含 `null`（缓存过期或没 Claude 数据时）——板子显示 `--` / em-dash。

## Claude Code statusline 集成

**修改 `~/.claude/statusline.sh`**（代码仓库不包含这个文件，但需要在 Mac 上做如下改动）：

```diff
-if [ "${live_five_pct:-}" != "null" ] && [ -n "${live_five_pct:-}" ] && [ -n "$input" ]; then
+if [ -n "$input" ]; then
   mkdir -p "$CACHE_DIR"
-  printf '%s' "$input" | jq '{rate_limits: .rate_limits}' \
+  printf '%s' "$input" | jq '{rate_limits: .rate_limits, context_window: .context_window, cached_at: (now | floor)}' \
     > "${CACHE_FILE}.tmp" 2>/dev/null \
     && mv "${CACHE_FILE}.tmp" "$CACHE_FILE" 2>/dev/null \
     || true
 fi
```

改完之后，每次你在 Claude Code 里发消息，`context_window` 也会被缓存，bridge 就能读到
Context 百分比。没有 Claude 活动的时候，cache 不变但 reset 时间还在随时间变化
（bridge 会按时间戳重新计算剩余时间）。

## 运行

```bash
# 一次编译 + 烧录
pio run -t upload

# 启动桥（前台，看日志）
python3 tools/mole-bridge.py

# 启动桥（后台，静默）
nohup python3 tools/mole-bridge.py --interval 2 >/dev/null 2>&1 & disown

# 临时调试看 verbose 日志
python3 tools/mole-bridge.py --verbose
```

`--interval` 默认 2 秒。低于 1 秒容易撞上 mole 启动开销；3~5 秒更省电。

### 端口冲突

板子的 Type-C 是 USB-CDC，烧录 (`pio run -t upload`) 和桥 (`mole-bridge.py`) 都会抢同一个
`/dev/cu.usbmodemXXXX`。所以每次烧录前要先 `pkill -f mole-bridge` 释放端口。
`tools/shot.sh` 自动做这件事，**你想复烧就跑 `pio run -t upload`，想截屏就跑 `shot.sh`**。

## 相关文件

| 文件 | 作用 |
|---|---|
| `src/main.cpp` | 板子侧主程序：解析 JSON、渲染 Ink-on-Paper 布局 |
| `src/display_bsp.{h,cpp}` | ST7305 底层驱动（SPI + 像素映射） |
| `src/rlcd_gfx.h` | Adafruit_GFX 适配层 |
| `tools/mole-bridge.py` | Host 桥：mole + Claude cache → 一行 JSON → serial |
| `tools/shot.py` | 从板子读回 framebuffer 存 PNG |
| `tools/shot.sh` | `shot.py` 的封装（自动暂停/恢复 bridge） |
| `platformio.ini` | PIO 配置 + lib_deps：Adafruit GFX、ArduinoJson、U8g2_for_Adafruit_GFX |
