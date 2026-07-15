# Ratio Mode WDT 根因、最终实现与串口保留结果

> **后续审计（2026-07-15）**：本页的稳定基线是 `e1a0120` 的两-source Ratio 固件与
> schema 1 的 Apply/Save Web Serial 协议。`669ce1c` 后续加入的 schema 2 实时配置层不属于
> 该稳定结论；它在完整产品镜像中重新触发 WDT，已暂缓移植。

## 结论

Ratio Mode 可以在当前 ESP32 上实施，但不能作为第五个持续计算的 Faust source 直接加入
现有完整 DSP 图。最终实现保留五个逻辑模式，只在 Faust 中保留两种物理 source：Poly 与
可编程 modal bank。String、Bar 和 Ratio 在音符、调律或 profile 事件发生时由固件计算
九个目标频率，再复用 modal bank；Cave 继续直接使用同一个 bank。

`e1a0120` 基线固件在 Ratio Mode、schema 1 串口轮询、ratio `< 1` 配置、五模式 MIDI
压力下均未再触发 WDT。`669ce1c` 的 schema 2 实时配置升级是高置信回归边界：即使没有网页、
串口或 MIDI 流量，仍约每 `5.6 s` 触发 CPU0 `Faust DSP Task` WDT。当前证据指向完整镜像的
代码、数据、链接布局与运行时交互，精确微机制尚未单独定位；因此暂不保留 schema 2。

## 根因 A/B

固定条件为 Faust `2.59.6`、ESP32 Arduino Core `2.0.4-cn`、240 MHz、44.1 kHz、
32 samples。音频 deadline 为 `725.624 µs`。

| DSP 图 | `compute(32)` p99 | 完整产品结果 |
|---|---:|---|
| 五 source，Ratio 原生分支 | `651.7–655.0 µs` | 约每 `5.6 s` WDT |
| 四 source，Ratio 复用 Cave | `639.9–645.4 µs` | 约每 `12.97 s` WDT |
| 两 source，Poly + modal bank | `589.5–599.6 µs` | Ratio、串口与 MIDI 压力下无 WDT |

## Schema 2 回归边界

| 镜像 | 大小 | SHA-256 | 观察 |
|---|---:|---|---|
| `e1a0120` normal | `1,200,912 B` | `c7b986ee5c4f8084e852abed950ce735c127a4931cbe9cf295c929d05d0b2f49` | 关闭 Bitwig、无网页/串口/MIDI 流量，观察窗口无 WDT |
| `669ce1c` normal | `1,208,128 B` | `1d000191e481aece7912d3d662e778cf4077e58281188ee0a9ea0468b34f5e8a` | 约每 `5.6 s` CPU0 `Faust DSP Task` WDT |

该 A/B 还在控制任务暂停与主循环暂停条件下复现过；因此只降低网页端发送频率不能解决问题，
必须回退固件侧的 `config_runtime.*`、schema 2 单项 patch、事件队列和 V2 存储路径。

`669ce1c` Save 同时写 V2 Cave blob 与 legacy frequency/mute keys，因此已成功 Save 的 Cave mute
可由 schema 1 固件继续读取；`0.01 Hz` 频率会按 legacy 格式四舍五入为整数。未 Save 的 RAM
修改本来就不跨重启，legacy 写入失败的异常情况也不能由旧固件恢复。

四-source 候选证明：只删除 Ratio 分支仍不够。真机 15 秒窗口内记录两次 CPU0 IDLE WDT，
当时左右均为 Poly，正在运行的仍是 `Faust DSP Task`。因此故障不是 Ratio 是否 active，
而是完整 audio callback 已没有足够阻塞时间让 CPU0 IDLE task 运行。

最终生成文件的独立 ESP32 benchmark 每组运行 100,000 blocks：

| Case | Median | p95 | p99 | Max | Deadline miss | Non-finite |
|---|---:|---:|---:|---:|---:|---:|
| inactive | `588.6 µs` | `589.2 µs` | `589.5 µs` | `589.979 µs` | 0 | 0 |
| single active | `596.8 µs` | `599.4 µs` | `599.6 µs` | `600.683 µs` | 0 | 0 |
| all active | `588.7 µs` | `594.6 µs` | `595.1 µs` | `595.667 µs` | 0 | 0 |

三组共 300,000 blocks、`191.6 s`，WDT 为 0。与五-source p99 相比，最终图释放约
`52–65 µs` 的持续计算时间；与四-source 基线相比释放约 `40–56 µs`。

## 最终架构

- 逻辑模式仍为 Poly、String、Bar、Cave、Ratio 五种，面板与 status 继续返回 `0..4`；
- Faust 的 `mode0/mode1` 只选择 Poly `0` 或 programmable modal bank `1`；
- String 使用 `fundamental × (index + 1)`；
- Bar 使用 `fundamental × 0.44444 × (index + 1.5)²`；
- Ratio 使用左右共享的九值 profile；
- 三种可移调模式都先通过 standard/alternate tuning 得到 fundamental，再计算九个频率；
- 所有目标仍由 DSP 裁剪到 `16–16000 Hz`，modal state、32-sample 更新、Decay 与
  anti-feedback 均保持不变；
- Ratio、String、Bar 与 Poly 进入时强制九个 resonator unmute；只有 Cave 重新应用当前 bank 的 mute；
- MIDI CC 0 的原四档 `0..126` 保持不变，值 `127` 专门选择 Ratio Mode。

## 串口与真机结果

最终 normal app 只写入 app0 `0x10000`；esptool 完成 data hash verify，没有读取 app0，
也没有写 bootloader、partition table、otadata 或 NVS 分区。

真机验证结果：

- Ratio active + 每秒一次 `status`：35 秒内 `35/35` 响应，WDT 0、reboot 0；
- 左右 MIDI Note On 将 status 从 `36/60` 更新到 `48/72`，fundamental 为
  `130.813/523.251 Hz`；
- 应用 `[0.125,0.5,1,1.5,2,3,4,5,9]` 后连续取得 33 个 Ratio status，随后恢复
  factory profile 并 Save；最终 revision `8`、dirty `false`；
- Poly/String/Bar/Cave/Ratio 五个 MIDI mode 均正确返回 `0/1/2/3/4`；String、Bar 与
  Ratio 的 MIDI Note On 均更新当前 note；
- Ratio active 时左右各 500 对、随后各 1000 对高速 Note On/Off，在串口持续捕获下均为
  WDT 0、reboot 0、panic/fault 0；
- 最终恢复产品 app 后，`hello/get/status` 与左右六个 Cave bank 全部可读；再次进入 Ratio、
  演奏左右音符、切回 Poly，并继续观察 20 秒，WDT 0、reboot 0；
- 设备最终保留 factory Ratio profile、dirty `false`，左右模式回到 Poly。

这说明轻量 schema 1 parser、Web Serial profile 和 Cave 配置可以保留；网页使用整组 Apply，
再由显式 Save 写入 Preferences。schema 2 的固件实时状态层不能与当前 WDT 余量一起保留。

## 构建与门禁

| Build | Program storage | RAM | Bin size | SHA-256 |
|---|---:|---:|---:|---|
| Normal | `1,195,133` | `51,300` | `1,200,912` | `c7b986ee5c4f8084e852abed950ce735c127a4931cbe9cf295c929d05d0b2f49` |
| `MIDI_DIAGNOSTICS=1` | `1,197,069` | `51,348` | `1,202,848` | `da9753f17229ffd63a475ae675f991314c1277304709fd09c02d072ff81473f3` |
| Schema 1 + Cave-only mute candidate | `1,195,245` | `51,300` | `1,201,024` | `50601e13464bf6e612aeade017817d132d2dbbd82040839ac0b641e9e8e1721a` |
| Candidate `MIDI_DIAGNOSTICS=1` | `1,197,157` | `51,348` | `1,202,928` | `866690f5d0d060cd86c3fd16e7d4722c75a76c1a02a1016cad376ddd67f59437` |

- `Wingie2.dsp` SHA-256：`9d8a356567e3d0e60be60c3ffd7b19e92f0f551f1c66894c4b36934e308470fb`；
- Faust generated C++ SHA-256：`396c8eb114a7705ce6a282ec79d28e41324eccb21c6537b7f0466dafee464496`；
- Ratio/String/Bar reference 与 firmware inventory：5 PASS；
- anti-feedback reference/analyzer：11 PASS；
- HTML 单文件与协议 inventory：3 PASS；
- flash tool tests：7 PASS；
- Ratio/Cave profile、serial parser 与 Tap Sequence host binaries：PASS；
- normal、diagnostic Arduino full build 与 `git diff --check`：PASS。

本次 schema 1 + Cave-only mute 候选只写入 app0 `0x10000`；独立 verify digest 通过。关闭
Bitwig 后，debug serial 只读观察 35 秒无 WDT；随后发送 35 个 `hello/status` 请求，得到
`35/35` 个合法响应、`malformed=0`、`watchdog=0`。schema 1 `get_cave` 读取左右六个 bank
的 mute mask 与 Ratio dirty 状态均正常，未访问任何 MIDI 端口。

## 尚未由自动测试证明

- 自动测试证明了时序、状态、频率映射、串口和稳定性，没有替代实体试听；仍需人工确认
  Poly/String/Bar/Cave/Ratio 的声音、wet/dry、快速变音高与 ratio `< 1` 的听感一致性；
- Ratio 双灯闪烁与 Save 灯效已避免代码层互相覆盖，仍需肉眼确认节奏和可读性；
- 当前串口 `set` 会连续写九个 DSP 参数，响应仍称为 `queued`，尚未实现严格的 audio-block
  双缓冲原子切换。若单个过渡 block 也必须是完整 snapshot，应另做 block-boundary flip；
- schema 1 页面仍是 Apply/Save 事务模型；schema 2 的 Live/Pending 文案与并发语义属于已暂缓
  的设计，不应作为当前产品行为。
