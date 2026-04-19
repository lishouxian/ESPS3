# 点亮 ST7305 屏幕

把 4.2" 反射屏从「黑屏」状态驱动起来。分两步：**原生驱动** → **接入 Adafruit_GFX**。

## 一、ST7305 驱动移植

ST7305 是单色反射式 LCD 控制器，典型特征：

- **Memory-in-pixel**：每个像素自带 SRAM，刷完一次图像后控制器可以断电，屏幕内容不消失
- **1 bpp**：只有黑白两色
- **SPI 接口**：3-wire SPI + DC 引脚 + RST
- **特殊地址窗口**：`CASET = 0x12..0x2A`，`RASET = 0x00..0xC7`（不是常规 0 开始）
- **像素打包**：每字节塞 8 个像素，但排列不是直觉上的「行优先扫描」——驱动层要做坐标换算

### 从 Waveshare demo 搬到 PlatformIO

Waveshare 官方 Demo 仓库：<https://github.com/waveshareteam/ESP32-S3-RLCD-4.2>，
其中 `02_Example/Arduino/08_LVGL_V8_Test/display_bsp.{h,cpp}` 就是 ST7305 驱动。

我们搬进来时做了两件事：

1. **去掉 LUT 优化**：原版可选用 `AlgorithmOptimization==3` 查表法（需要在 PSRAM 里分配 ≈360KB 的 x/y→索引+位掩码 查找表）。首次点灯没必要，直接用 `==2` 的位移法。

2. **只保留 landscape 400×300**：原版兼容竖屏（300×400）和横屏两套坐标变换，简化为单一横屏版本。

驱动底层用的是 **ESP-IDF 的 `esp_lcd_panel_io_spi` API**，Arduino-ESP32 会捎带整个 IDF，直接 `#include <esp_lcd_panel_io.h>` 即可，不用自己手搓 SPI 事务。

文件位置：`src/display_bsp.{h,cpp}`，调用入口：

```cpp
DisplayPort rlcd(MOSI=12, SCK=11, DC=5, CS=40, RST=41, W=400, H=300);
rlcd.RLCD_Init();
rlcd.RLCD_ColorClear(ColorWhite);
rlcd.RLCD_SetPixel(x, y, ColorBlack);
rlcd.RLCD_Display();  // 把 framebuffer 一次性 DMA 过去
```

### 像素坐标变换（ST7305 特殊之处）

landscape 模式下，每字节 8 bit 对应 2 像素（x 方向）× 4 像素（y 方向）的一个小块：

```cpp
uint16_t inv_y   = height - 1 - y;       // y 轴反向
uint16_t byte_x  = x >> 1;                // 每 2 px 一组
uint16_t block_y = inv_y >> 2;            // 每 4 px 一组
uint32_t index   = byte_x * (height/4) + block_y;   // 行序：列优先
uint8_t  bit     = 7 - ((inv_y & 3) * 2 + (x & 1));
```

如果直接按常规「每行 width/8 字节，y * stride + x/8」那套算，屏幕上出来的会是乱码。这是 ST7305 的 memory map 决定的，不是驱动 bug。

## 二、接入 Adafruit_GFX

原生驱动只有 `SetPixel` 一个绘图原语，没有线/矩形/文字。与其自己手搓，不如让它继承 Adafruit_GFX —— 重载一个虚函数 `drawPixel()`，整套 API（`drawLine` / `fillRect` / `drawCircle` / `print` / 位图 / 字体）就全来了。

### platformio.ini 加依赖

```ini
lib_deps =
    adafruit/Adafruit GFX Library @ ^1.11.11
```

PIO 首次 `pio run` 会自动从 PlatformIO Registry 拉。国内网络需要走代理（同 brew）。

### 适配层：`src/rlcd_gfx.h`

```cpp
class RlcdGfx : public Adafruit_GFX {
 public:
  RlcdGfx(DisplayPort& port, int16_t w, int16_t h)
      : Adafruit_GFX(w, h), port_(port) {}

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    port_.RLCD_SetPixel(x, y, color ? ColorBlack : ColorWhite);
  }
  void clear(uint8_t c = ColorWhite) { port_.RLCD_ColorClear(c); }
  void flush()                       { port_.RLCD_Display(); }
};
```

调用：

```cpp
RlcdGfx gfx(rlcd, 400, 300);
gfx.clear();
gfx.setTextSize(3);
gfx.setCursor(12, 10);
gfx.print("ESPS3 / ST7305");
gfx.drawRect(0, 0, 400, 300, 1);
gfx.flush();     // 真正把 framebuffer 推到屏幕
```

### 使用习惯

Adafruit_GFX 的坐标 / color 语义：

- `color == 0` → 白，`color != 0` → 黑（跟常见墨水屏相反但跟 1-bit LCD 通用）
- `setTextColor(1)` 黑字，`setTextColor(0)` 白字
- `setTextSize(n)` 是整数倍放大 5×7 默认字体，高 DPI 屏下能勉强看
- `fillRect(x, y, w, h, 0)` 是「擦掉一块矩形」，用来局部刷新非常方便

## 三、局部刷新 & 性能观察

Demo 里每秒只重绘一小块计数器区域：

```cpp
gfx.fillRect(120, 160, 260, 50, 0);   // 白色擦除计数器旧值
gfx.setCursor(128, 168);
gfx.setTextSize(4);
gfx.printf("%02lu:%02lu:%02lu", h, m, s);

uint32_t t0 = millis();
gfx.flush();           // DMA 整屏 15000 字节 framebuffer
uint32_t t1 = millis();
```

实测结果：

```
tick 00:00:01  flush=0 ms  free_heap=349372
tick 00:00:02  flush=0 ms  free_heap=349372
...
```

`flush` 返回 0 ms 是因为 `esp_lcd_panel_io_tx_color` 走 DMA 异步提交，
CPU 立刻就解放了，SPI 控制器在后台把 15000 字节以 10 MHz 推出去（≈12 ms）。
对 Arduino 这种 loop 程序来说，刷一次屏基本零成本。

视觉观察：memory-in-pixel 特性下**没有撕裂、没有闪烁**，新画面"渐变"到位，
比常见 TFT 的 tearing、e-ink 的全局闪烁都更丝滑。

## 四、小坑 & 注意事项

- **framebuffer 要用 `MALLOC_CAP_DMA`**：不要偷懒塞 PSRAM，SPI DMA 不吃 PSRAM 地址（这版驱动已经处理，`MALLOC_CAP_DMA`）。15000 字节对内部 RAM 毫无压力。
- **Adafruit_GFX 的 `drawFastHLine` / `drawFastVLine`**：基类默认实现是循环调 `drawPixel`，对 1-bit 屏足够快；没必要重载。
- **不要频繁 `RLCD_Init`**：初始化会触发硬复位 + 全屏清白，会产生肉眼可见的闪烁。正常使用只在 setup() 里调一次。
- **`RLCD_Display` 会把整帧重传**：即使只改了一个像素也全推。对这种低刷新率场景够用；想做局部窗口刷新要改驱动（重写 CASET/RASET 地址范围和内存上传长度）。

## 五、下一步可做

- 用 U8g2 字体替换默认 5×7，拿到更好看的中/英文字体
- GFX 支持 XBM 位图，可以做 logo / 图标
- 引入 LVGL（Waveshare demo 里有现成 port，用我们的 `RLCD_Display` 作 flush 回调）
- 把 `RLCD_TE_PIN` (GPIO6) 接上，等 TE 中断再 DMA，避免极限情况下的撕裂
