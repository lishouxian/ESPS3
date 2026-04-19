# 硬件规格与引脚速查

Waveshare **ESP32-S3-RLCD-4.2** 开发板。

- 产品页: <https://www.waveshare.net/shop/ESP32-S3-RLCD-4.2.htm>
- Wiki: <https://docs.waveshare.net/ESP32-S3-RLCD-4.2>
- 原厂 Demo: <https://github.com/waveshareteam/ESP32-S3-RLCD-4.2>
- 原理图 PDF: <https://www.waveshare.net/w/upload/e/e6/ESP32-S3-RLCD-4.2-schematic.pdf>

## 核心规格

| 项目 | 参数 |
| --- | --- |
| 模组 | ESP32-S3-WROOM-1-**N16R8** |
| CPU | Xtensa LX7 双核 @ 240 MHz |
| Flash / PSRAM | 16 MB / 8 MB（PSRAM 为 OPI/八线） |
| SRAM / ROM | 512 KB / 384 KB |
| 无线 | Wi-Fi 2.4 GHz + BLE 5 |
| USB | ESP32-S3 原生 USB-CDC（Type-C 口直接烧录，无需外置 CH340） |
| 供电 | Type-C + 18650 锂电池座 + RTC 纽扣电池座 (PH1.0) |

## 显示屏（RLCD，反射式，无背光，常显）

| 项目 | 参数 |
| --- | --- |
| 尺寸 / 分辨率 | 4.2" / **300 × 400**（横屏 400×300） |
| 驱动 IC | **ST7305**（单色 memory-in-pixel，SPI 接口） |
| 颜色深度 | 1 bpp（黑 / 白） |
| 总线 | SPI（Demo 使用 `SPI3_HOST`） |
| 触控 | **无** |

ST7305 数据手册: <https://www.waveshare.net/w/upload/5/5d/ST_7305_V0_2.pdf>

> ST7305 特点：像素自带存储（memory-in-pixel），刷新后无需持续驱动即可保持画面，功耗极低；
> 只有黑白两色；刷新用 SPI 传 framebuffer。

## 引脚速查表

### LCD（ST7305 · SPI）

| 信号 | GPIO |
| --- | --- |
| MOSI / SDA | **GPIO12** |
| SCK / SCL | **GPIO11** |
| DC | **GPIO5** |
| CS | **GPIO40** |
| RST | **GPIO41** |
| TE | **GPIO6** |

### I²C 总线（RTC / 温湿度 / 音频 codec 共用）

| 信号 | GPIO |
| --- | --- |
| SDA | **GPIO13** |
| SCL | **GPIO14** |

I²C 从机地址：

| 器件 | 地址 |
| --- | --- |
| PCF85063 RTC | `0x51` |
| SHTC3 温湿度 | `0x70`（芯片默认） |
| ES8311 音频 codec | `0x18` |
| ES7210 mic ADC | `0x40` |

### I²S 音频（ES8311 播放 + ES7210 双麦 ADC）

| 信号 | GPIO |
| --- | --- |
| MCLK | **GPIO16** |
| BCLK | **GPIO9** |
| WS / LRCK | **GPIO45** |
| DIN（麦克风输入） | **GPIO10** |
| DOUT（喇叭输出） | **GPIO8** |
| PA_EN（功放使能） | **GPIO46** |

喇叭接口：MX1.25 2-pin。

### SD 卡（SDMMC 1-bit 模式）

| 信号 | GPIO |
| --- | --- |
| CLK | **GPIO38** |
| CMD | **GPIO21** |
| D0 | **GPIO39** |

### 按键

| 按键 | GPIO | 触发电平 |
| --- | --- | --- |
| BOOT | **GPIO0** | 低 |
| KEY（用户自定义） | **GPIO18** | 低 |
| PWR | 硬件电源控制 | — |

### 其他

- 电池电压检测：**ADC1 Channel 3**
- LED：CHG（充电）、WRN（警告）—— 硬件直驱，不占 GPIO
- 其他 GPIO 从 2×8P 2.54 mm 排针引出

## 板载 IC 数据手册

- ES8311 音频 codec: <https://www.waveshare.net/w/upload/6/65/ES8311.DS.pdf>
- PCF85063 RTC: <https://www.waveshare.net/w/upload/c/c0/Pcf85063atl1118-NdPQpTGE-loeW7GbZ7.pdf>
- SHTC3 温湿度: <https://www.waveshare.net/w/upload/3/33/SHTC3_Datasheet.pdf>
- ST7305 LCD 驱动: <https://www.waveshare.net/w/upload/5/5d/ST_7305_V0_2.pdf>
