# PlatformIO 开发环境搭建

本项目用 **PlatformIO** 作为 Arduino 框架下的构建与烧录工具链。
选它而非 Arduino IDE 是因为：配置即代码（`platformio.ini`）、依赖可锁版本、全流程 CLI、换机器不用重装库。

## 一、安装

### macOS（本项目采用）

```bash
brew install platformio
```

这会安装 `pio` 命令以及底层需要的 Python 环境（PIO 自己管一套独立 Python 虚拟环境，不污染系统）。

验证：

```bash
pio --version
```

### 其他系统

参考官方文档：<https://docs.platformio.org/en/latest/core/installation/index.html>。

PlatformIO 也有 VS Code 插件：装完 `pio` 之后，VS Code 里装 "PlatformIO IDE" 扩展即可获得图形化按钮（可选，CLI 已够用）。

## 二、项目结构

PlatformIO 工程标准目录：

```
ESPS3/
├── platformio.ini     # 工程配置：板子型号、依赖、编译选项
├── src/               # 源码（main.cpp 或 main.ino）
├── lib/               # 本项目私有库（可选）
├── include/           # 公共头文件（可选）
├── test/              # 单元测试（可选）
└── .pio/              # 构建产物（git 忽略）
```

## 三、选板子（ESP32-S3-WROOM-1 N16R8）

PlatformIO 的板子定义里，通用的 ESP32-S3 开发板是 `esp32-s3-devkitc-1`。
但我们这块是 **N16R8**（16 MB Flash + 8 MB OPI PSRAM），必须在 `platformio.ini` 里显式声明，否则 PSRAM 用不上、分区表也对不上。

关键 `build_flags`：

| flag | 作用 |
| --- | --- |
| `-DBOARD_HAS_PSRAM` | Arduino 层识别到 PSRAM |
| `-DARDUINO_USB_MODE=1` | 使用原生 USB CDC 作为 Serial |
| `-DARDUINO_USB_CDC_ON_BOOT=1` | 上电即启用 USB CDC（否则要手动调用 Serial 才出来） |

关键板级参数：

| 参数 | 值 |
| --- | --- |
| `board_upload.flash_size` | `16MB` |
| `board_build.arduino.memory_type` | `qio_opi`（Flash QIO + PSRAM OPI 八线） |
| `board_build.partitions` | `default_16MB.csv` |

## 四、烧录与串口

插上 Type-C 线，ESP32-S3 原生 USB 会枚举成 CDC 串口。在 macOS 上看到的通常是：

```
/dev/cu.usbmodemXXXX
```

PIO 能自动探测，不用手动指定端口（烧不上再 `--upload-port`）。

常用命令：

```bash
pio run                          # 仅编译
pio run -t upload                # 编译并烧录
pio run -t upload -t monitor     # 编译、烧录、开串口
pio device monitor                # 只开串口（波特率默认 9600，项目里改成 115200）
pio device list                   # 列出可用端口
```

ESP32-S3 原生 USB 一般能自动进下载模式，**不用手动按 BOOT**。如果偶发烧不上：

1. 按住 BOOT，按一下 RST（或 PWR），松开 BOOT
2. 再跑 `pio run -t upload`

## 五、常见坑

- **Python 版本**：系统 Python 3.14 太新时部分工具链有兼容问题；用 `brew install platformio` 会带自己的 Python，规避。
- **PSRAM 没识别**：检查 `build_flags` 是否有 `-DBOARD_HAS_PSRAM`，以及 `memory_type` 是否是 `qio_opi`。
- **Serial 没输出**：没加 `-DARDUINO_USB_CDC_ON_BOOT=1` 时，板子重启后 CDC 要等 `Serial.begin()` 才枚举，前面几行日志会丢。
- **分区表不对**：16MB Flash 不能用默认的 4MB 分区，要显式指定 `default_16MB.csv`。
