# 设计方向：Ink on Paper

## 为什么选这个方向

这块 4.2″ 反射屏物理上就是**墨 + 纸**：不发光、常开、断电保持。放桌上像一张印刷卡片，
不像一块屏。设计上刻意放大这个直觉：

- 衬线字体做主数字（像报纸统计栏里"29%"的感觉）
- 标签用追踪大写无衬线（像编辑部小标题）
- 分隔用**墨线**（双线在顶、单线在段落之间），不用边框不用阴影
- 1-bit 屏天然没有灰度 / 透明 / 渐变 —— 全盘接受限制

选它之前过了另外两个方向（Braun 仪表、Terminal Brutalist，mockup 见
`mockups/design-directions.html`）。Ink on Paper 赢在**最不像 AI 做的**，
也最能体现反射屏这个材料。

## 版面（400×300 真实像素）

```
┌──────────────────────────────────────────────────────────┐
│ Mac mini · Apple M4 · 32.0GB · up 8d 12h                 │  strap
│────────────────────────────────────────────────────────── │  rule

│ CPU LOAD ──────────────┬─ MEMORY ──────────────────────── │  label + rule
│                         │                                  │
│ 29%                     │ 63%                              │  hero num
│ ──────                  │ ──────                           │  rule under num
│ [████░░░░░░░░░░]         │ [███████░░░░░░░]                │  progress bar
│ load 2.93               │ 20.4 / 32.0 GB                  │  meta
│                                                            │
│ DOWN 0.17 MB/s          UP 0.15 MB/s                       │  net (no sparkline)
│                                                            │
│ CLAUDE SESSION ────────────────────────────────────────── │  section header
│   Context 29%  [████░░░░░░░░]                           — │
│   5 h     31%  [████░░░░░░░░]                       3h15m │
│   7 d      7%  [█░░░░░░░░░░░]                       4d19h │
│                                                            │
│─────────────────────────────────────────────────────────── │  rule
│ DISK  146 / 228 GB [███░░░]    last 3s · by mole & xian   │  footer
└──────────────────────────────────────────────────────────┘
```

## 字体选择（全部来自 U8g2）

| 用途 | 字体 | 高度 | 说明 |
|---|---|---|---|
| 大数字（hero） | `u8g2_font_ncenB24_tn` | 24 px | Century Schoolbook Bold，只含数字 + 标点 |
| "%" 后缀 | `u8g2_font_ncenB14_tr` | 14 px | Century Bold 14，紧贴大数字右上 |
| Hero meta / Claude key | `u8g2_font_ncenR10_tf` | 10 px | Century Regular，`_tf` 含全 Unicode（中点 `·`） |
| Claude percent | `u8g2_font_ncenB12_tr` | 12 px | Century Bold 12 |
| 顶部 strap | `u8g2_font_ncenR12_tf` | 12 px | Century Regular 12，含 `·` |
| 标签（CPU LOAD / UP / DOWN / DISK / CLAUDE SESSION） | `u8g2_font_helvB08_tf` | 8 px | Helvetica Bold 8，追踪大写 |

选 `_tf`（full Unicode）而不是 `_tr`（restricted）的地方都是因为要渲染 `·` (U+00B7)。
追踪大写的"tracked small-caps"视觉是通过**全字母大写 + 小号 Helvetica 粗体**实现的，
U8g2 没有原生 tracking API，但小号粗体默认间距已经够紧凑。

## 关键 Y 坐标（见 `src/main.cpp`）

```cpp
constexpr int Y_STRAP_BASELINE   = 14;
constexpr int Y_STRAP_RULE       = 22;

constexpr int Y_HERO_LABEL       = 44;   // "CPU LOAD"
constexpr int Y_HERO_LABEL_RULE  = 40;   // trailing rule right of label
constexpr int Y_HERO_NUMBER      = 76;   // "29"
constexpr int Y_HERO_NUMBER_RULE = 84;   // thin rule under number
constexpr int Y_HERO_BAR_TOP     = 88;
constexpr int Y_HERO_META        = 112;

constexpr int Y_NET              = 136;

constexpr int Y_CLAUDE_HDR       = 164;
constexpr int Y_CLAUDE_HDR_RULE  = 160;
constexpr int Y_CLAUDE_R1        = 184;
constexpr int Y_CLAUDE_R2        = 202;
constexpr int Y_CLAUDE_R3        = 220;

constexpr int Y_FOOT_RULE        = 248;
constexpr int Y_FOOTER           = 268;
```

U8g2 的 `setCursor(x, y)` 的 `y` 是**baseline**（字的下沿），不是 top。
规划布局时要留出 ascent（字符上沿到 baseline 的距离），比如 `ncenB24_tn` ascent ≈ 20 px，
所以 baseline=76 时字顶部在 y=56 左右。

## 分隔逻辑

全屏只有 3~4 条横线，节奏：

- **顶 strap 下方**：单线 `Y_STRAP_RULE=22`
- **CPU LOAD / MEMORY 标签右侧**：线从标签结束延伸到列右缘（`draw_hero_half` 内）
- **大数字下方**：薄线，紧跟在数字与进度条之间
- **CLAUDE SESSION 标题右侧**：线延伸到屏幕右缘（和 label 规则同一风格）
- **footer 上方**：整条横线 `Y_FOOT_RULE=248`

没有 box 框，没有内部 padding 模拟的"卡片"。分隔完全用线和留白。

## 不做什么

- ❌ 圆角（1-bit 抗锯齿是假话，越圆越糊）
- ❌ 阴影 / 渐变（屏幕物理不支持）
- ❌ 图标（纯文字 + 线条更编辑气质；箭头都不用，写 `DOWN` / `UP` 更清爽）
- ❌ 斜体（U8g2 斜体 glyph 覆盖不全；用衬线字形本身暗示 "editorial" 就够了）
- ❌ 网速 sparkline（早期有，后来删了——光是两个数字比小竖线群信息量高得多）
- ❌ TOP PROCESSES（早期有，后来用 CLAUDE SESSION 替换——你更关心 context 而不是 `WindowServer` 吃了多少 CPU）

## 未来可能加什么

- 按键（GPIO0 BOOT / GPIO18 KEY）切换"屏"：目前只有 system 屏，可以加一屏专门的 Claude
  详情（消息数、token/s、成本累计……）
- 签名微动画：数字变化时旧值留一条细"墨影"三秒淡出，模拟换版的痕迹（需要双缓冲 diff）
- TE 中断同步刷屏（接 GPIO6）避免极限情况下的撕裂
