# ESPS3

Waveshare **ESP32-S3-RLCD-4.2** 开发板的学习与折腾记录，使用 **PlatformIO + Arduino** 框架开发。

## 文档

- [`docs/01-hardware.md`](docs/01-hardware.md) — 硬件规格、引脚速查、板载 IC
- [`docs/02-platformio-setup.md`](docs/02-platformio-setup.md) — PlatformIO 环境搭建、烧录流程、常见坑
- [`docs/03-first-flash.md`](docs/03-first-flash.md) — 首次烧录实录（验证工具链 + 串口打印）
- [`docs/04-display.md`](docs/04-display.md) — 点亮 ST7305 屏幕 + 接入 Adafruit_GFX

## 快速开始

```bash
# 装 PlatformIO（macOS）
brew install platformio

# 插上板子的 Type-C（原生 USB-CDC，会出现 /dev/cu.usbmodemXXXX）

# 编译 + 烧录 + 开串口
pio run -t upload -t monitor
```

工程目录：

```
ESPS3/
├── platformio.ini     # 板子型号与编译配置
├── src/main.cpp       # 当前 sketch（串口自检）
└── docs/              # 过程中记录的资料
```
