# 设计方向：Ink on Paper（最终版）

## 为什么选这个方向

这块 4.2″ 反射屏物理上就是**墨 + 纸**：不发光、常开、断电保持。放桌上像一张印刷卡片，
不像一块屏。设计上刻意放大这个直觉：

- 衬线字体做主数字（像报纸统计栏里"29%"的感觉）
- Bold 无衬线小号全大写做小标签（像编辑部小标题）
- 段落之间**只靠字体层级 + 留白**划分 —— 不用任何横线、box、装饰
- 1-bit 屏天然没有灰度 / 透明 / 渐变 —— 全盘接受限制

选它之前过了另外两个方向（Braun 仪表、Terminal Brutalist，mockup 见
`mockups/design-directions.html`）。Ink on Paper 赢在**最不像 AI 做的**，
也最能体现反射屏这个材料。

## 最终版面（400×300 真实像素）

```
 Mac mini · Apple M4 · 32.0GB · up 8d 13h               ← 顶 strap (ncenR12)


 CPU LOAD                   MEMORY                       ← 标签 (helvB12)

 20%                         63%                         ← 主数字 (ncenB24_tn)
 ▓▓▓▓▓░░░░░░░░░░             ▓▓▓▓▓▓▓▓▓▓▓▓▓░░             ← 进度条 (14px)



 CLAUDE SESSION                                          ← 区块标签 (helvB12)

 Context  40%  ▓▓▓▓▓▓░░░░░░░░░                          —
 5 h      46%  ▓▓▓▓▓▓▓▓░░░░░░░                      2h42m
 7 d       7%  ▓░░░░░░░░░░░░░░                      4d18h



 DISK 146 / 228 GB              last 0s · by mole & xian  ← footer (ncenR12)
```

**整屏无一根横线**，段落靠字体层级和留白自然分开。

## 字体选择（全部来自 U8g2）

| 用途 | 字体 | 高度 | 说明 |
|---|---|---|---|
| 顶部 strap / 正文衬线 | `u8g2_font_ncenR12_tf` | 12 px | Century Schoolbook Regular，含 Unicode（`·`） |
| 所有小标签（CPU LOAD / MEMORY / CLAUDE SESSION / DISK） | `u8g2_font_helvB12_tf` | 12 px | Helvetica Bold，全大写 |
| Hero 大数字 | `u8g2_font_ncenB24_tn` | 24 px | Century Bold，只含数字 + 标点 |
| Hero `%` 后缀 | `u8g2_font_ncenB18_tr` | 18 px | 紧贴主数字右上 |
| Claude key（Context / 5 h / 7 d） | `u8g2_font_ncenR12_tf` | 12 px | 衬线正文 |
| Claude percent | `u8g2_font_ncenB14_tr` | 14 px | 衬线粗体，当前值强调 |
| Claude reset time | `u8g2_font_ncenR12_tf` | 12 px | 右对齐，和 key 字重呼应 |

选 `_tf`（full Unicode）而不是 `_tr`（restricted）是因为要渲染中点 `·` (U+00B7)
和 em-dash `—` (U+2014)。

## 关键 Y 坐标（见 `src/main.cpp`）

```cpp
constexpr int Y_STRAP_BASELINE = 20;

constexpr int Y_HERO_LABEL     = 56;   // CPU LOAD / MEMORY
constexpr int Y_HERO_NUMBER    = 100;  // 20 / 63
constexpr int Y_HERO_BAR_TOP   = 110;
constexpr int HERO_BAR_H       = 14;

constexpr int Y_CLAUDE_HDR     = 166;
constexpr int Y_CLAUDE_R1      = 196;
constexpr int Y_CLAUDE_R2      = 228;
constexpr int Y_CLAUDE_R3      = 260;

constexpr int Y_FOOTER         = 294;
```

U8g2 的 `setCursor(x, y)` 的 `y` 是 **baseline**（字的下沿），规划布局时要留出
ascent。比如 `ncenB24_tn` ascent ≈ 20 px，所以 baseline=100 时字顶部在 y≈80 附近。

## 显式 **不** 做的事（设计决策的删减）

这些早期存在过、经评估后明确移除。

| 元素 | 原因 |
|---|---|
| 顶部 masthead "MACMINI-2 · STATUS" | 重复信息，strap 里的硬件行更有用 |
| 右上角 HEALTH 徽章（圆形） | 圆形在 1-bit 里锯齿明显；mole 的 health score 意义模糊 |
| 顶 strap 下方双横线 | 1-bit 小屏上线条偏重，字体层级已经够 |
| CPU LOAD / MEMORY 标签后延横线 | 同上；和列对齐本身就在分区 |
| 大数字下方薄线 | 和下方进度条间太密，信息冗余 |
| Hero 下方的 meta 行（`load 2.01 AVG`, `20.4/32 GB`） | 百分比已经说完故事，meta 是"给极客看"的噪声 |
| NETWORK 区整块（Down/Up + sparkline） | 日常空闲时 0.00/0.00，没信息量；有需要再单独做屏 |
| TOP PROCESSES | 3 行太小看不清，用户 `top` 更合适 |
| DISK 后面的进度条 | disk 占用变化极慢，不是 glance 信息 |
| CLAUDE SESSION 标题后延横线 | 字体层级够；视觉更静 |
| footer 上方分割线 | 同上 |

## 为什么保留这些

- **进度条**：% 数字告诉你"现在是多少"，条告诉你"离极限多远"，两者互补，不可替代
- **Claude reset time**：知道"还多久回血"才能规划是否 `/clear`
- **DISK 数值**（`146/228 GB`）：做开发时查 Xcode/Docker 生成的大文件有无占爆，
  不需要每秒更新但需要能扫到
- **顶 strap 完整行**：`Mac mini · Apple M4 · 32.0GB · up 8d 13h` 一次给出身份 +
  uptime，是"这块屏正连着谁"的答案

## 不做什么（风格层面）

- ❌ 圆角 / 阴影 / 渐变（1-bit 不支持，硬做只会糊）
- ❌ 横线 / box / 卡片框（太重，和 "paper" 的静气相反）
- ❌ 图标（纯文字更编辑气质；箭头都不用，写 `Down` / `Up` 更好）
- ❌ 斜体（U8g2 斜体 glyph 覆盖不全；serif 字形本身暗示 "editorial" 就够了）

## 未来可能的方向

- **按键切屏**：KEY (GPIO18) / BOOT (GPIO0) 切换到「其他屏」—— 比如网络细节、系统进程、
  日历卡片 …… 每块屏还是 Ink on Paper 的风格，但回答不同问题
- **Context 专屏**：Claude 深度数据（消息数 / tokens in-out / 成本累计）
- **动画签名**：数字变化时旧值留一条细"墨影"3 秒淡出，模拟换版痕迹（需要双缓冲 diff）
- **TE 中断同步刷屏**（接 GPIO6）避免极限刷新下的撕裂
