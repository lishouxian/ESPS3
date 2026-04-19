# ESPS3

Waveshare **ESP32-S3-RLCD-4.2** 开发板：4.2″ 反射式 LCD（300×400，1-bit，常开、无背光）。
使用 **PlatformIO + Arduino** 框架开发。

当前玩法：**Mac 系统面板 + Claude Code 会话状态** 常显于桌面。
设计方向 Ink on Paper —— 像一张每 2 秒重新印一次的状态卡片。

![current](https://img.shields.io/badge/state-Ink%20on%20Paper%20v2-black?style=flat-square)

## 文档

- [`docs/01-hardware.md`](docs/01-hardware.md) — 硬件规格、引脚速查、板载 IC
- [`docs/02-platformio-setup.md`](docs/02-platformio-setup.md) — PlatformIO 环境搭建、常见坑
- [`docs/03-first-flash.md`](docs/03-first-flash.md) — 首次烧录实录
- [`docs/04-display.md`](docs/04-display.md) — 点亮 ST7305 + 接入 Adafruit_GFX
- [`docs/05-mole-dashboard.md`](docs/05-mole-dashboard.md) — **当前架构：Mac 状态 + Claude 状态桥接**
- [`docs/06-ink-on-paper.md`](docs/06-ink-on-paper.md) — 设计方向、字体、布局理由
- [`docs/07-shot-tool.md`](docs/07-shot-tool.md) — 从板子直接读回 framebuffer 存 PNG
- [`docs/08-deployment.md`](docs/08-deployment.md) — **部署到新 Mac**（一键 installer + launchd 自启）

## 快速开始

### 只想把一块装好的板子挂到 Mac 上

```bash
git clone https://github.com/lishouxian/ESPS3 ~/ESPS3
cd ~/ESPS3
tools/install.sh --autostart   # 一键：依赖 + wrappers + launchd 自启
```

插上板子，屏幕自动亮起。详见 [docs/08-deployment.md](docs/08-deployment.md)。

### 开发机（要改代码 / 烧固件）

```bash
# 装 PlatformIO + 编译烧录
brew install platformio
pio run -t upload

# 装运行依赖
tools/install.sh

# 日常
esps3-bridge   # 跑 bridge（前台）
esps3-shot     # 截当前屏存 /tmp/esps3-shot.png
```

## 工程结构

```
ESPS3/
├── platformio.ini           板级 / lib 配置
├── src/
│   ├── main.cpp             板子主程序：Ink on Paper 渲染 + JSON ingest
│   ├── display_bsp.{h,cpp}  ST7305 底层 SPI 驱动
│   └── rlcd_gfx.h           Adafruit_GFX 适配层
├── tools/
│   ├── mole-bridge.py       Mac 端桥：mole + Claude cache → USB CDC
│   ├── shot.py              从板子读 framebuffer 存 PNG
│   └── shot.sh              shot.py 的封装（自动停/启 bridge）
├── mockups/
│   └── design-directions.html  三个方向对比 HTML（仅设计选型阶段用）
└── docs/                    过程笔记
```

## 当前功能

- ✅ 顶栏硬件 strap：`Mac mini · Apple M4 · 32.0GB · up 8d 13h`
- ✅ CPU LOAD 大数字 + 进度条
- ✅ MEMORY 大数字 + 进度条
- ✅ CLAUDE SESSION：Context % / 5h % / 7d %，带进度条和重置时间
- ✅ DISK 占用 + 更新时间戳 + 归属标签
- ✅ 纯字体层级 + 留白分区，**整屏无横线**

## 硬件要点

| | |
|---|---|
| 模组 | ESP32-S3-WROOM-1 N16R8（16 MB Flash + 8 MB OPI PSRAM） |
| 屏幕 | ST7305 反射式 LCD，400×300，1-bit B/W，常开、memory-in-pixel |
| USB | 原生 USB-CDC（Type-C 直接烧录 + 串口数据） |

详见 [`docs/01-hardware.md`](docs/01-hardware.md)。
