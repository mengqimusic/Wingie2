# Ratio Mode WDT 根因、最终实现与串口保留结果

> **后续审计（2026-07-16）**：本页前半部分保留 `e1a0120` 两-source Ratio 固件、schema 1
> Apply/Save 协议与 `669ce1c` schema 2 回归的历史证据。当前产品实现已升级为下文记录的
> 轻量 schema 3；原先“必须回退 V2 / 当前保持 schema 1”的判断已被后续布局实验和真机验证取代。

## 结论

Ratio Mode 可以在当前 ESP32 上实施，但不能作为第五个持续计算的 Faust source 直接加入
现有完整 DSP 图。最终实现保留五个逻辑模式，只在 Faust 中保留两种物理 source：Poly 与
可编程 modal bank。String、Bar 和 Ratio 在音符、调律或 profile 事件发生时由固件计算
九个目标频率，再复用 modal bank；Cave 继续直接使用同一个 bank。

`e1a0120` 基线固件在 Ratio Mode、schema 1 串口轮询、ratio `< 1` 配置、五模式 MIDI
压力下均未再触发 WDT。`669ce1c` 的 schema 2 实时配置升级是高置信回归边界：即使没有网页、
串口或 MIDI 流量，仍约每 `5.6 s` 触发 CPU0 `Faust DSP Task` WDT。后续 schema 3 实验将问题
进一步定位为完整镜像中 DSP 热路径的链接与取指布局临界点，而不是 Ratio 本身或“任何 V2
存储/单项设置协议都不能保留”；最终解决方案见“Schema 3 最终结果”。

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

该 A/B 还在控制任务暂停与主循环暂停条件下复现过；因此只降低网页端发送频率不能解决问题。
当时回退了 `config_runtime.*`、schema 2 单项 patch、事件队列和 V2 存储路径，以建立可验证的
稳定基线；后续实验已经证明，V2 存储路径本身并非必须删除，真正边界是完整镜像的 DSP 热路径布局。

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

这在当时说明轻量 schema 1 parser、Web Serial profile 和 Cave 配置可以作为稳定回退基线；
网页当时使用整组 Apply，再由显式 Save 写入 Preferences。当前产品行为已由下文的轻量
schema 3 取代：连接读取设备快照，单项修改实时写设备，只有持久化需要 Save。

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
  的历史设计，不应与当前轻量 schema 3 的产品行为混淆。

## Schema 3 最终结果（2026-07-16）

### 协议与产品行为

最终 schema 3 只增加 `get_settings` 与 `set_param` 两条协议路径。它没有恢复 schema 2 的
`config_runtime.*`、事件队列、heartbeat、自动 polling 或 mutex，也没有设备到网页的实时同步
刷新。网页连接时读取一次完整设备快照，手动 Refresh 可重新读取；每项编辑直接、单向写入设备，
无需 Apply，只有 Save 需要按键确认并写入 Preferences。

当前设置页支持左右独立的 Mode、Threshold，以及 A3、Tuning、Gain、MIDI、Ratio、Cave 等其他
设置项目。网页不显示或编辑仅运行态的 Mix、Decay、Volume，也不请求可能很快过期的 Source、Note、
Fundamental、Active Cave status。连接读取、单项写入、Save、断线重连、Import/Export、Ratio/Cave
revision 与 dirty 状态均经过协议和浏览器测试。

网页 Save 与实体双键 Save 均在 `loopTask` 串行执行，control task 只提交请求；初始化完成前
不处理 MIDI。这避免全局 `Preferences` 对象被 control、Web Serial 或启动期 MIDI tuning 并发打开。

### WDT 根因与布局实验

未做布局修正的完整 schema 3 镜像将 `mydsp::compute` 放在 flash 地址 `0x400d4664`，静默状态
仍会触发 CPU0 `IDLE` WDT；当时 CPU0 正在运行 `Faust DSP Task`。这复现于没有网页请求、自动
轮询、MIDI 或后台配置任务的条件，因此不是网页发送频率、Bitwig、USB MIDI 端口竞争或 Ratio
是否 active 导致。

把辅助实现移入普通 `.cpp`，以及单独尝试 64-byte alignment，均未消除 WDT。这两组反证说明
“代码拆文件”或“任意对齐”本身不是修复。最终构建通过 `Wingie2/build_opt.h` 启用
`-mtext-section-literals`，并将 `mydsp::compute` 放入 IRAM；最终入口地址为 `0x40080474`，IRAM
末端为 `0x4009ece4`，剩余 `4,892 B`。该余量已纳入最终镜像检查，后续增加 IRAM 代码时必须
重新核对，不能把本次结果泛化为 IRAM 仍有宽裕空间。

### 真机验证

最终 normal 与 `MIDI_DIAGNOSTICS=1` 镜像均通过以下门禁：

- 静默 `15 s`：WDT 0、reboot 0；
- 静默 `35 s`：WDT 0、reboot 0；
- 连续 `35` 个串口请求：全部获得合法响应，WDT 0、reboot 0；
- 完整 schema 3 功能测试：左右独立参数、A3/Tuning、Gain、MIDI、Ratio、Cave 均通过；
- Save 后重启持久化、全量配置恢复后再次 Save 与重启：均通过；
- 只向 `USB MIDI DevicePort 1` 发送 Channel 16 A3 14-bit CC 后，A3 更新为 `440.25 Hz`，
  alternate Cave 立即随之重调；随后原配置 Save/reboot 恢复通过；
- 最终设备运行 normal 镜像，恢复原配置后再次静默 `35 s`，WDT 0、reboot 0。

diagnostic 第一次完整功能循环曾在 candidate reboot 后报告一个左侧高 bank Cave 频率超过
`0.0051 Hz` 容差；自动恢复原配置成功。随后相同 diagnostic 测试连续 3 次、normal 测试 1 次
全部通过，未再复现。该单次观测保留在结果中，不据此宣称已定位新的固件回归。

所有真机写入仍只写 app0 `0x10000` 并执行 verify，没有读取或备份设备 app0，也没有改写
bootloader、partition table、otadata 或 NVS 分区。没有访问 `/dev/cu.usbmodem_1`；MIDI 只打开
上述 Device Port 1 完成 A3 测试，从未打开或发送到 Port 2。

真机阶段 `/dev/cu.usbserial-11310` 出现过两次 macOS `Errno 6: Device not configured`：一次在
diagnostic app0 写入 `24%` 时，一次发生在 MIDI 发送器启动之前。两次检查均没有其他进程占用该
串口，设备节点随后自行恢复，完整重写/verify 或测试重试通过。因此该现象证据指向 USB debug
serial 链路瞬断，不支持“另一个进程正在写串口”或 MIDI Port 2 竞争的解释。

### Cave V2 跨版本结果

Cave V2 频率现以 `0.01 Hz` 分辨率显示、编辑和保存，范围仍为 `16.00–16000.00 Hz`。Cave mute
跨已发布 v3.2 升级后的保留已经真机验证；因此 V2 Cave 数据与 mute 兼容性不再是回退 schema 1
或放弃 schema 3 的理由。

### 最终构建

| Build | Program storage | RAM | Bin size | SHA-256 |
|---|---:|---:|---:|---|
| Schema 3 normal | `1,201,769 B` | `51,316 B` | `1,207,536 B` | `f6de06cd3dd11d3051228e9891c6f59dbb7a7ae86ed62fbb0c8c43fe356b7b27` |
| Schema 3 `MIDI_DIAGNOSTICS=1` | `1,203,653 B` | `51,364 B` | `1,209,424 B` | `8ae8f15fe2608b73965eb5ac706070b4a8447396c73c550377c76d6fa8e25a34` |

两套镜像均由最终提交候选完整重建；`mydsp::compute` 地址同为 `0x40080474`，size 同为
`0x3b5d`，IRAM 末端同为 `0x4009ece4`，相对 `0x400a0000` 上限剩余 `0x131c`（`4,892 B`）。
该函数仍调用 flash text 中的数学库函数；本修复只针对正常运行时的取指/WDT 布局，不表示
`compute` 已可在 cache-off 或 ISR 环境中安全执行。

`Wingie2/Wingie2.cpp` 是 Faust 生成物；再次运行 `faust2esp32` 会覆盖 `esp_attr.h` include 与
`compute` 的 `IRAM_ATTR`。替换生成输出时必须重新施加这两个布局标记，并运行
`test_faust_compute_uses_iram_with_local_literals`；`Wingie2/build_opt.h` 本身不由 Faust 生成。

### Ratio 与未来 MPE / 14-bit Pitch Bend

当前 Ratio 复用左右两条既有 modal source 路径，只改变 9 个共鸣器相对同一 fundamental 的比例，
没有增加第五套持续运行的音频引擎。因此 MPE channel state、14-bit pitch-bend 解析和
fundamental 更新默认属于 MIDI event/control 路径，主要消耗 flash、DRAM 与事件处理时间，不要求
再复制 Ratio DSP，也不应占用本次仅剩 `4,892 B` 的 IRAM 热路径。按当前证据，Ratio 不构成 MPE
实现的系统性阻断。

但这只证明传输、状态与现有 voice 数可以共存，不等于已经证明任意多音 MPE。当前乐器仍是左右
两条 channel engine，Poly 也只有现有的 3-note 轮转参数；若未来 MPE 要求每个 member channel
长期拥有独立 voice、包络或更多 DSP 实例，那是新的 voice-allocation 与音频负载设计，必须另做
full-scale benchmark。14-bit pitch bend 的 bend range、无新事件时是否锁存、以及 Ratio/String/
Bar/Cave/Poly 各模式如何解释 per-note bend 也尚未决定；实现前需要单独确认，而不能由固件擅自加入
smoothing、回落或超时。
