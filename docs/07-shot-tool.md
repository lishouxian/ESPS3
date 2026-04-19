# Framebuffer 截屏工具

板子真实渲染的像素能直接拿到，用来替代对着屏幕拍照——改完代码 → `pio run -t upload`
→ `tools/shot.sh` → 拿到 `/tmp/esps3-shot.png`，这就是 LCD 此刻展示的像素原样。

## 原理

1. 板子 `src/main.cpp` 在 `consume_line()` 里识别一条特殊输入 `SHOT\n`
2. 板子把 framebuffer 用带帧头的格式发回：
   ```
   SHOT-BEGIN <bytes> <w> <h>\n
   <raw framebuffer bytes>
   \n
   SHOT-END\n
   ```
   （400×300 单色 = 15000 字节，115200 baud 下 ~1.3 秒）
3. `tools/shot.py` 读到 `SHOT-BEGIN`，读 N 字节，按 ST7305 横屏 1-bit 打包规则解回
   400×300 pixel 阵列，用 Pillow 保存 PNG

ST7305 的 landscape 寻址公式（详见 `src/display_bsp.cpp`）：

```python
inv_y   = h - 1 - y
byte_x  = x // 2
block_y = inv_y // 4
index   = byte_x * (h // 4) + block_y
bit     = 7 - ((inv_y % 4) * 2 + (x % 2))
```

在 framebuffer 里，**bit=1 表示 LCD 未驱动的像素（反射屏 = 白纸色）**，
bit=0 表示黑墨。ST7305 是 memory-in-pixel 的典型行为，复位默认全白。

## 使用

```bash
# 基础：假设桥已经在跑，脚本会暂停桥 → 截图 → 重启桥
tools/shot.sh                 # → /tmp/esps3-shot.png

# 自定义输出位置
tools/shot.sh ./snap.png

# 桥没在跑，不想自动启动桥：直接调底层
python3 tools/shot.py --out ./snap.png
```

## 串口占用

板子的 USB-CDC 同时是烧录口、桥的数据口、截屏的数据口，macOS 同一端口同一时刻只能一个进程
持有。`shot.sh` 处理了：

1. `pgrep -f mole-bridge.py` 判断桥是否在跑
2. 若在跑：`pkill` 杀掉 → `sleep 0.8`
3. 跑 `shot.py` 抓屏
4. 如果一开始有桥，用 `nohup ... &` 在后台重启

如果你手动调过 bridge，脚本不会误杀其他进程（只匹配 `mole-bridge.py` 路径）。

## 性能 & 注意事项

- **单次耗时**：~2 秒（1.3s framebuffer 传输 + 0.5s Python 解码 + 0.2s 其他）
- **不影响屏幕内容**：板子只读 framebuffer，不触发重绘
- **解码 CPU**：`shot.py` 用纯 Python for-loop 逐像素解包，120k 次循环，M 系列芯片 ~0.3s
  （懒得 NumPy 化；截屏不是高频操作）
- **图像是"此刻"而非"实时流"**：每次 `shot.sh` 产出一张 PNG。如果想要动画 / 实时流，
  需要做双缓冲或在板子侧加流模式——目前用不到

## 为什么不用 Chrome headless 截 HTML 模拟图？

早期想法。问题：

- Web 字体（EB Garamond、Playfair）和 U8g2 bitmap 字体（Century Schoolbook、Helvetica 位图）
  **永远不会完全匹配**——字距、ascent/descent、pixel hinting 都不一样
- 1-bit LCD 的"锯齿美学"要从实际 framebuffer 里看才准
- HTML 模拟图只能告诉你"意图上大概长这样"，截真实 framebuffer 告诉你"板子**就是**这样"

所以 `mockups/design-directions.html` 只作为**方向选型**阶段的参考，
落到实现之后一律用 `shot.sh`。
