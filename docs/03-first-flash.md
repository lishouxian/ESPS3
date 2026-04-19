# 首次烧录实录

目标：打通「PlatformIO 编译 → 烧录 → 串口验证」链路，确认板子、Flash、PSRAM 都被正确识别。

## 0. 前置：给 Homebrew 和 PlatformIO 设代理

国内网络访问 `formulae.brew.sh` / `github.com` 经常卡，brew、pio 首次下载会失败。

shell 里先导出代理（对应 `~/.zshrc` 里定义的 `f1()` 函数内容）：

```bash
export http_proxy="http://127.0.0.1:7890"
export https_proxy="http://127.0.0.1:7890"
export HTTP_PROXY="$http_proxy"
export HTTPS_PROXY="$https_proxy"
export all_proxy="socks5://127.0.0.1:7890"
export ALL_PROXY="$all_proxy"
```

在 zsh 里直接调 `f1` 亦可。PlatformIO 会自动读取 `HTTP_PROXY` / `HTTPS_PROXY` 用于下载 platform / framework。

## 1. 装 PlatformIO

```bash
brew install platformio
pio --version   # PlatformIO Core, version 6.1.19
```

Homebrew 会把 pio 装在 `/opt/homebrew/bin/pio`，自带独立 Python 3.14 venv 在 `/opt/homebrew/Cellar/platformio/<ver>/libexec`。

## 2. 板子连接与识别

Type-C 线插到板子的 Type-C 口（不是供电口——这块板子只有一个 Type-C，自身就是 USB-CDC）。

```bash
pio device list
```

找到这一段就对了：

```
/dev/cu.usbmodem1101
--------------------
Hardware ID: USB VID:PID=303A:1001 SER=44:1B:F6:CC:7F:A8 LOCATION=1-1
Description: USB JTAG/serial debug unit
```

- VID `303A` = Espressif
- PID `1001` = ESP32-S3 JTAG/Serial 原生 USB（不是 CH340/CP2102 这种桥接器）
- 无需额外装驱动

## 3. 工程最小骨架

```
ESPS3/
├── platformio.ini
├── src/main.cpp
└── .gitignore   ( .pio/ 要忽略 )
```

`platformio.ini` 关键配置（完整文件见仓库根目录）：

```ini
[env:esp32-s3-rlcd-4_2]
platform = espressif32
board    = esp32-s3-devkitc-1
framework = arduino

board_build.arduino.memory_type = qio_opi   ; Flash QIO + PSRAM OPI 八线
board_build.partitions          = default_16MB.csv
board_upload.flash_size         = 16MB

monitor_speed = 115200
build_flags =
    -DBOARD_HAS_PSRAM
    -DARDUINO_USB_MODE=1
    -DARDUINO_USB_CDC_ON_BOOT=1
```

`src/main.cpp` 只打印芯片信息：

```cpp
#include <Arduino.h>
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("=== ESPS3 boot ===");
  Serial.printf("Chip model : %s rev %d\n", ESP.getChipModel(), ESP.getChipRevision());
  Serial.printf("CPU freq   : %lu MHz\n", ESP.getCpuFreqMHz());
  Serial.printf("Flash size : %lu MB\n", ESP.getFlashChipSize() / (1024UL*1024UL));
  Serial.printf("PSRAM size : %lu bytes\n", (unsigned long)ESP.getPsramSize());
}
void loop() { Serial.printf("[%lu] alive\n", millis()); delay(2000); }
```

## 4. 首次编译

首次运行 `pio run` 会下载：

- `platform-espressif32`
- `framework-arduinoespressif32` (~200 MB)
- `toolchain-xtensa-esp-elf`
- `tool-esptoolpy`

总计约 400–500 MB，本机实测带代理 ~1 分钟。完成后：

```
RAM:   [=         ]   5.8% (used 18852 bytes from 327680 bytes)
Flash: [          ]   4.1% (used 266333 bytes from 6553600 bytes)
========================= [SUCCESS] Took 59.05 seconds =========================
```

> "Flash 6553600 bytes" 是单个 app 分区的大小（`default_16MB.csv` 里 app0 / app1 各 6.25 MB，另外还有 NVS、OTA data、SPIFFS 等分区）。

## 5. 烧录

```bash
pio run -t upload
```

ESP32-S3 原生 USB CDC 无需手动按 BOOT，pio 自动进下载模式、写入、硬复位。~42 秒完成（主要花在 flash 写入与 hash 校验）。

## 6. 读串口的坑：`pio device monitor` 在后台报错

直接 `pio device monitor` 在交互终端里完美工作，但在 **非 TTY 环境**（脚本、后台、自动化工具）里会报：

```
termios.error: (19, 'Operation not supported by device')
```

原因：miniterm 试图对 stdin 做 `tcgetattr`，没有 TTY 时失败。

**替代方案**：直接用 PlatformIO 自带的 pyserial，写一段 Python 读 N 秒：

```bash
/opt/homebrew/Cellar/platformio/6.1.19_1/libexec/bin/python3 -c "
import serial, time
s = serial.Serial()
s.port = '/dev/cu.usbmodem1101'
s.baudrate = 115200
s.timeout = 1
s.dtr = False; s.rts = False
s.open()
# 触发一次复位（DTR/RTS 脉冲，ESP32-S3 原生 USB 也支持）
s.setDTR(False); s.setRTS(True);  time.sleep(0.1)
s.setDTR(True);  s.setRTS(False); time.sleep(0.1)
s.setDTR(False); s.setRTS(False)
s.reset_input_buffer()
end = time.time() + 6
while time.time() < end:
    line = s.readline()
    if line: print(line.decode('utf-8', errors='replace'), end='')
s.close()
"
```

日常开发还是直接 `pio device monitor` 最顺手，Ctrl+C 退出。

## 7. 成果：串口输出

```
=== ESPS3 boot ===
Chip model : ESP32-S3 rev 0
CPU freq   : 240 MHz
Flash size : 16 MB
PSRAM size : 8386295 bytes     ≈ 8 MB (OPI PSRAM 正确识别)
Free heap  : 370508 bytes
[607] alive, free heap=370508
[2607] alive, free heap=370508
```

✅ Flash 16 MB、PSRAM 8 MB 都识别到，PlatformIO 配置正确，工具链贯通。
