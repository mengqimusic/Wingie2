# Ratio Mode、Cave 与纯 HTML 串口配置结果

> 2026-07-15 更新：本页记录的原生第五 Faust source 在完整产品中会触发 WDT，已由
> [Ratio Mode WDT 根因、最终实现与串口保留结果](2026-07-15-ratio-mode-wdt-results.md)
> 替代为两-source 实现。本页以下内容保留为原候选的配置与串口验证记录。

## 交付内容

- Ratio Mode 为第五模式，左右共用 9-value profile，范围 `0.125–32`、精度 `0.001`，
  factory profile 为 `[1,2,3,4,5,6,7,8,9]`；
- 键盘、Tap Sequencer 与 MIDI Note On 沿用 String/Bar 的基频路径；
- Ratio Mode 双灯同步以 `500 ms` on/off 闪烁；
- Cave 保留左右各 3 个 bank、9 个频率、9 个 mute 与既有 Preferences key；
- `Tools/wingie_config.html` 是无外链的单文件 HTML，通过 Web Serial 管理 Ratio/Cave；
- SoftAP、ESP32 WebServer、CDN、Node 与 Python 均不进入产品配置路径。

## 自动测试

| Gate | 结果 |
|---|---|
| Ratio/Cave host profile contract，`-Wall -Wextra -Werror` | PASS |
| Serial protocol host parser，`-Wall -Wextra -Werror` | PASS |
| Ratio DSP reference 与 generated inventory | 4 PASS |
| HTML 单文件、无外链、协议 inventory 与 JavaScript parse | 3 PASS |
| `git diff --check` | PASS |

DSP reference 确认 standard/alternate tuning 都先生成 fundamental，再乘共享 ratio；覆盖
ratio `< 1`、`16–16000 Hz` 边界、NaN 拒绝，以及 generated C++ 中恰好 9 个共享控件。

## Chromium 页面验证

使用 `agent-browser` 和隔离的 Web Serial mock 验证：

- 无设备时 Ratio/Cave tab、disabled 状态与 unsupported-browser 提示正常；
- `hello` 后读取 1 个 Ratio profile、左右共 6 个 Cave bank 与 status；
- Ratio cents、左右 effective Hz 和频率裁剪状态正确显示；
- Cave active/inactive bank 显示 Live/Pending；
- Ratio Apply、Cave Apply 与 Save 状态转换通过；
- 页面无 console error，CSS/JavaScript/界面资源全部内联。

## 完整固件构建

工具链为 ESP32 Arduino Core `2.0.4-cn`，目标 `esp32:esp32:esp32`。

| Build | Program storage | Global variables | SHA-256 |
|---|---:|---:|---|
| Normal | `1,198,557 / 1,310,720` | `51,300 / 327,680` | `cf04ba0036d4cbddd2cb2c00863f639ba02a67e73f1d97c008736983e7a8fec8` |
| `MIDI_DIAGNOSTICS=1` | `1,200,557 / 1,310,720` | `51,348 / 327,680` | `b3d9bc40b6efdcc4909ad4ba4abf0ad438b1c93764d9b83a130259389130c05f` |

## USB 真机验证

只把 diagnostic app 写到 app0 `0x10000`；esptool 完成 hash verify，没有写 bootloader、
partition table、otadata 或 NVS。

自动事务结果：

- 4 次重启在 Preferences 完成前共返回 59 个 `busy`，成功握手后才允许读取/写入；
- 连续 60 个 `get/status/get_cave` 请求无 malformed protocol line；
- Ratio 全 snapshot set、invalid value 原子拒绝与 revision conflict 通过；
- inactive Cave bank set、invalid frequency 原子拒绝通过；
- Save、EN reset、重启持久化通过；
- diagnostic `p` 单字符命令与 `@...` 协议分流通过；
- 最终 Ratio 已恢复 `[1,2,3,4,5,6,7,8,9]`，六个 Cave bank 与 mute 已恢复原值，
  所有 dirty 状态为 false。

## 启动竞态修复

首次真机恢复测试暴露了两个启动期问题：

1. 协议响应原先用两次 Serial write 输出 `<` 与 JSON，control task 日志可能插入中间；
2. 主循环可能在 control task 加载 Preferences 前处理配置请求，使 Cave RAM 看起来是全零。

修复后每个响应使用单次 `Serial.write`，并以 `serial_config_ready` 阻止启动期命令。测试中
曾被错误 Save 的 Cave keys 通过 NVS 日志中的覆盖前值恢复；没有读取 app0，恢复后又完成
Save/reset 对照验证。

## 仍需人工确认

USB、配置状态与持久化已在真机验证。以下声音和面板行为需要在设备旁人工完成：

- 第五模式双灯 `500 ms` 闪烁与 Save 灯效不冲突；
- 左右键盘、Tap Sequencer 与 MIDI Note On 在 Ratio Mode 中正确移调；
- ratio `< 1` 的 subharmonic、快速音符/ratio 变化、wet/finite/稳定性听测；
- Cave active bank 的实时声音、inactive bank 切换后声音和逐共鸣器 mute；
- 退出 Ratio Mode 后 Poly/String/Bar/Cave 无听感回归。
