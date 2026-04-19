# 部署到一台新 Mac

目标：一条命令就能在任意 Mac 上把 bridge 装好，插上板子就能显示；可选开机自启。

## 先决条件

- macOS 上安装 [Homebrew](https://brew.sh)
- Python 3（`brew install python@3`，macOS 自带的 `/usr/bin/python3` 也行）
- 已烧录好固件的 ESP32-S3-RLCD-4.2 板子（仅在开发机需要 PlatformIO 刷固件，见 [03-first-flash.md](03-first-flash.md)）

## 一键安装

```bash
git clone https://github.com/lishouxian/ESPS3 ~/ESPS3
cd ~/ESPS3
tools/install.sh             # 基本安装
tools/install.sh --autostart # 同时注册 launchd 开机自启
```

`install.sh` 会：

1. 检查 / 安装 `mole`（`brew install tw93/tap/mole`）
2. 在 `~/.esps3/venv` 建独立 Python venv，装 `pyserial` + `Pillow`
3. 在 `~/.esps3/bin` 生成两个包装脚本：
   - `esps3-bridge` — 跑 bridge
   - `esps3-shot` — 抓板子 framebuffer 存 PNG
4. 把 `~/.esps3/bin` 追加进 `~/.zshrc` 的 PATH（幂等）
5. 打补丁 `~/.claude/statusline.sh`，让它缓存 `context_window` + `rate_limits`（会先备份原文件）
6. `--autostart` 时写一个 `~/Library/LaunchAgents/com.esps3.bridge.plist`，用 `launchctl load` 激活

## 运行

**有自启**（`--autostart` 安装）：什么都不用做。登录就会跑，拔了再插会自动重连。

**没自启**：

```bash
esps3-bridge                  # 前台
# 或后台：
nohup esps3-bridge --interval 2 >/dev/null 2>&1 & disown
```

## 随时截一张当前画面

```bash
esps3-shot                    # → /tmp/esps3-shot.png
esps3-shot --out ./snap.png   # 自定义路径
```

`esps3-shot` 会自动处理 "bridge 占着端口 → shot 抢过来 → 还回去" 的流程：

- 手工启动的 bridge → pkill + nohup 重启
- launchd 托管的 bridge → pkill 后等 launchd 在 5 秒内自己拉起

## 端口自动发现

bridge 和 shot 都按 **USB VID `0x303A`（Espressif）**自动找板子串口。新 Mac 上无论枚举成
`/dev/cu.usbmodem1101`、`1201`、`2301` 都能识别，不用改配置。

想强制指定端口：

```bash
esps3-bridge --port /dev/cu.usbmodem1101
```

## 卸载

```bash
tools/install.sh --uninstall
```

这会：

- 卸载 launchd agent（如果装了）
- 删除 `~/.esps3/`（venv + wrappers + log）
- **保留** `~/.zshrc` 里的 PATH 行（需要手动删）
- **保留** `~/.claude/statusline.sh`（和它的 `.bak.*` 备份）

## launchd + 开发体验的小坑

`--autostart` 激活后，launchd 会在 bridge 退出后 5 秒内重启它。烧录固件时 bridge 会抢端口，需要临时停掉：

```bash
launchctl unload ~/Library/LaunchAgents/com.esps3.bridge.plist
pio run -t upload
launchctl load   ~/Library/LaunchAgents/com.esps3.bridge.plist
```

或者最快：

```bash
launchctl kickstart -k gui/$(id -u)/com.esps3.bridge   # 等待它 respawn
```

`shot.sh` 已经识别 launchd 场景、不会和 launchd 抢着重启 bridge。

## 日志

```bash
tail -f ~/.esps3/log/bridge.out  # 标准输出
tail -f ~/.esps3/log/bridge.err  # 错误
```

Bridge 打开端口、丢失端口、重连的事件都在这里。

## launchd plist PATH 的坑

launchd 下 shell 环境极简，默认只有 `/usr/bin:/bin`。`mole` 依赖 `system_profiler`
（`/usr/sbin`）和 `sysctl`（`/sbin`），**这两条没在 PATH 里会导致 mole 输出的 model /
cpu_model / uptime_seconds 变空**。

`install.sh` 生成的 plist 已经把完整 PATH 写进去了：

```xml
<key>PATH</key><string>/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin</string>
```

如果你看到屏幕上 strap 只剩 `32.0GB · up 0m`、model/cpu 消失，99% 是手改过 plist 时
漏掉了这两个目录。

## 生命周期图

```
登录 → launchd 读 plist → esps3-bridge 启动 → open /dev/cu.usbmodemXXXX
  ↓
每 2s：mole status --json + read Claude cache
  ↓                                     ↑
JSON line → /dev/cu.usbmodemXXXX → ESP32-S3 → ST7305 帧缓冲
  ↑                                                ↓
esps3-shot SHOT\n ←─── 15000 bytes ←─── SHOT-BEGIN/END

拔板子 → bridge 抛 SerialException → 3s 后重试 → 插回即接上
bridge 崩溃 → launchd 5s 内重启
```
