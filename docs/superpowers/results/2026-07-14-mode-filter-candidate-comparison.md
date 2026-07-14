# Wingie2 Mode Filter 技术方案对比

## 结论先行

八条路径中，真正进入当前真机试听池的是 Coupled Q、Coupled D、block-rate
rotation/modal bank、normalized-lattice E 和 normalized-lattice R。它们都已有完整候选
在 ESP32 上运行的证据；有 complete timing capture 的 Q、D、modal bank 和 E 均为
0 hard deadline miss，R 的 complete timing 未测。这里的“进入试听池”只表示没有被
当前唯一硬门禁淘汰，不表示已经选中、音色等价或适合安装到产品。

当前证据又分三层：Q、E、R 已运行正常固件并有用户试听原话；D 和 modal bank 只有
instrumented complete-product 硬件运行证据，正常固件、wet/dry 和试听仍未测。E 有可归属
的部分 MIDI 压力记录；R 没有完整压力矩阵；E/R 都没有足以标记 PASS 的 mute/unmute
真机记录。设备在 2026-07-14 最后运行的是 Coupled Q 正常固件，用户评价为
`我听起来没问题。`

另外三条路径不在当前试听池：历史 direct form 是产品基线而不是新候选；block-rate
decay direct form 虽然时序很好，但真机出现“所有通道都没有 wet”的功能失败；直接
`fi.tf2np` 在固定 Faust 2.59.6 和真实 alternate-tuning 图下不能完成生成，因此没有可上机
的完整候选。

## 阅读口径：历史拒绝与当前试听资格

本文同时保存两个时间层，二者不能互相覆盖：

- **历史结论**按每次实验当时预注册的门禁记录。旧门禁要求 100,000 blocks 中
  p99 `<= 580.5 us`、max `< 725.6 us`、deadline misses `= 0`，并在首个失败处停止。
- **当前资格**按用户 2026-07-14 的决定记录：
  `唯一硬门禁是‘方案是否可以在硬件上跑得动’，其他都等真机试用`。

因此，Q、D、modal bank 和 E 的旧 p99 FAIL 仍是准确的历史结果，但不再自动淘汰；E
的 output-bound 和 R 的 16 Hz FFT 标签也仍保留，但交给真机判断。被旧门禁跳过的项目
仍写“未测”，不会补写成 PASS。能够上机、能够持续运行、具有 audible wet、用户觉得
可用和最终安装是五个不同状态。

## 八种路径总表

| 路径 | 递归/状态结构 | 系数更新 | 静态响应关系 | 快速跳变机制 | 完整生成 | 硬件运行 | Wet/试听证据 | 历史结论 | 当前资格 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1. 历史 `pm.modeFilter` direct form | 每模态二阶 direct-form 延迟状态 | 频率、T60 与递归均为原有 sample-rate 图 | 固定系数响应为历史基准 | 旧延迟状态直接套用新系数；没有独立收缩证明 | 是，alternate tuning 完整 | 是；当代 complete p99 `715.9 us`、0 miss | 历史恢复后左右 wet 经确认正常 | 保留为 main 产品源基线 | 基线，不是替换候选；当前设备并非该镜像 |
| 2. Block-rate decay direct form | 与历史 direct form 相同 | 仅 decay pole-radius 每 32 samples 准备；其余保持原图 | settled 与动态矩阵近似历史等价 | 状态坐标不变，只降低 `pow` 成本 | 是 | 是；complete p99 `456.3 us`、0 miss | String ch2 后用户报告 `当前所有通道都没有 wet` | 因物理 wet 失败拒绝并恢复历史镜像 | 不在试听池；可计时运行不等于完整音频可用 |
| 3. Faust `fi.tf2np` 直接替换 | epsilon 投影反射系数的 normalized ladder | 审计版本仍有逐 sample `pow/sqrt` | 目标是同系数 transfer-function 实现；完整产品未验证 | normalized 状态意在承受系数变化，但真实图未生成 | **否**；alternate tuning 下 `recursive composition A~B` | 未构建、未刷写 | 未测 | 固定工具链下不可执行旧计划 | 不具备上机资格；需新设计而非恢复旧计划 |
| 4. Coupled Q | 每模态 `rho R(theta)` 二维收缩旋转；输出缩放 quadrature state | `rho` 每 32 samples；状态逐 sample | 保留主要峰和目标 pole，不是历史 numerator 等价 | 旧二维状态在新旋转/收缩系数下继续，不 reset | 是，alternate tuning 完整 | 是；complete p99 `714.6 us`、0 miss | 2026-07-14 正常固件已试听：`我听起来没问题。` | 旧 p99 门禁拒绝，后续正常固件曾被跳过 | 保留；当前设备运行此方案，未安装到 main |
| 5. Coupled D | 与 Q 同一核心，quadrature 输出再做一拍差分 | 与 Q 相同，另有逐 sample 差分记忆 | 同 pole/主要峰目标；差分改变低频和增益，不是历史严格等价 | 核心状态连续，输出差分记忆也连续 | 是，alternate tuning 完整 | 是；complete p99 `715.2 us`、0 miss | 正常固件、wet/dry、试听未测 | 旧 p99 门禁拒绝并跳过正常固件 | 保留为待试听候选 |
| 6. Block-rate rotation/modal bank | 独立 `rho R(theta)` 二维 rotation bank | `rho/sin/cos` 与标定每 32 samples；状态逐 sample | 单模态一个主要窄峰；允许不同于历史响应 | 任意 note/ratio 跳变后，原状态在下一 block 的新旋转下继续 | 是，alternate tuning 完整 | 是；complete p99 `717.1 us`、0 miss | 正常固件、压力、试听未测 | 旧 p99 门禁拒绝；另有 `mode_changed` UI path 回归 | 保留，但正常试听前须正视已知 UI 回归 |
| 7. Normalized-lattice E | 两段 normalized lattice；共享 `1-z^-2` 输入差分并做 `1/(c1*c2)` 输出归一化 | 反射系数与归一化每 32 samples；状态逐 sample | 固定系数严格复现历史 transfer function | normalized 状态在新反射系数下继续；输出 scale 可瞬时变大 | 是，alternate tuning 完整 | 是；generated p99 `395.8 us`；complete 两组 0 miss | 用户：`E 也非常好，而且音量明显比 R 要高。`；wet/dry 正常 | 旧 output-bound 与 p99 门禁拒绝 | 保留；已试听，未最终选中 |
| 8. Normalized-lattice R | 与 E 相同 lattice，直接输出 raw terminal tap | 与 E 相同，但无输入差分和输出 reciprocal | pole 相同；DC、宽带响应、模态电平和增益有意不同 | normalized 状态连续；无 E 的大输出归一化 | 是，alternate tuning 完整 | 是；generated p99 `370.6 us`；正常固件已运行 | 用户：`听起来没问题`；左右 wet 与 dry/thru 正常 | 旧 16 Hz FFT 标签拒绝；后续 Task 8 被跳过 | 保留；已试听，complete timing/压力仍未测 |

## 1. 历史 direct-form `pm.modeFilter`

历史产品每边有 9 个独立二阶模态，固定系数传递函数为：

```text
H(z) = (1 - z^-2) / (1 - 2 rho cos(theta) z^-1 + rho^2 z^-2)
rho = 0.001^(1 / (T60 * SR))
```

这里的状态是 direct-form 延迟量：它们是差分方程过去输入/输出的坐标，而不是显式的
能量坐标。频率快速跳变时，旧状态不会清空，而是立即被新的分母系数解释。这不等于
必然失稳，但也没有像 rotation 或 normalized lattice 那样逐步给出收缩边界。

它定义了本文的固定系数音高、T60 和增益基准，也保留原有左右 UI、alternate tuning、
mode-change 输入链和 250 ms mute ASR。达到 16 kHz 上限时还有历史自动输出 mute。仓库
中的 `Wingie2.dsp`、generated C++ 和 header 仍是这一实现；2026-07-14 设备上的 Q 镜像
不改变这个 Git 事实。

当代测量中，generated-class p99/max 为 `739.5/801.817 us`，有 `1,308` misses；
complete-product p99/max 为 `715.9/724.921 us`，0 miss。complete 结果包含 blocking I2S
write 等待，不能当作纯 DSP CPU 时间。历史恢复镜像的 wet 正常，但这不证明任意快速
换音和未来 ratio 跳变都安全。

## 2. Block-rate decay direct form

这条路径只把两个通道的 decay pole-radius `pow` 准备移到 32-sample block 边界；T60
smoothing、mode envelope 和 direct-form 递归仍按原图推进。它没有更换状态坐标，因此
回答的是“把昂贵 decay 计算降频能省多少”，而不是“换一种递归能否改善跳变手感”。

三组 180-case settled 矩阵和 24-case dynamic 矩阵均无数值失败；complete-product
p99/max 降到 `456.3/469.642 us`，0 miss。可是 String channel 2 压力后，用户报告
`当前所有通道都没有 wet`。MIDI counters 和 marker 正常，现有证据没有确定根因，不能
把故障简化成 parser、CPU 或某个通道的问题。该实现已在 archive 中 revert，Poly/Bar
和 A/B 按当时门禁跳过；它不因今天放宽数值门禁而重新获得试听资格。

## 3. Faust `fi.tf2np` 直接替换

固定 `filters.lib` 中的 `fi.tf2np` 不是一个轻量 `fi.tf2` 别名，而是 epsilon-protected
reflection coefficients 加 normalized-ladder `allpassnnlt`。代表性动态 T60 单滤波器
展开为每 sample 1 个 `pow` 和 4 个 `sqrt`；关闭 alternate tuning 的缩减产品仍有两边
合计每 sample 2 个 `pow`、24 个 `sqrt`。

更根本的问题是保留真实 alternate-tuning 路径时，Faust 2.59.6 以
`recursive composition A~B` 失败，完整产品没有生成。因而它没有固件镜像、硬件运行、
wet 或试听证据。这个结果只关闭“在固定工具链中直接替换”的旧路线，不证明 normalized
ladder 数学上不可行；后来的 custom normalized lattice 正是独立新设计。

## 4. Coupled Q

Q 把每个模态写成 `rho R(theta)` 的二维旋转收缩。零输入时，旋转不改变二维能量，
`rho < 1` 负责衰减；频率跳变只改变旋转角，旧状态继续推进。输出取 quadrature state
并乘 `2/rho`，所以它保留目标 pole 和主要峰，却不承诺历史 `1-z^-2` numerator 的静态
增益或快速跳变瞬态。

Q 完整 host matrix 为 556 rows、0 failure，最大静态 pitch error `0.162502 cents`。
generated-class p99/max `544.7/546.517 us`，complete-product `714.6/723.625 us`，均
0 miss。旧实验因 p99 拒绝并跳过正常固件；新规则后构建并运行了 1,191,248-byte Q
正常镜像，用户评价 `我听起来没问题。` 当前设备保持此镜像，但没有新的完整压力矩阵，
也没有修改 main 产品源。

## 5. Coupled D

D 复用 Q 的二维核心，在 quadrature 输出后减去一拍历史值。这个差分增加一个输出记忆，
并引入 `z=1` 零点，改变 DC、低频和相对增益；它没有注册为历史 transfer function 的
严格等价实现。频率跳变时核心状态和差分记忆都不清空。

D 同样完成 556 host rows、0 failure，最大静态 pitch error `2.007266 cents`。
generated-class p99/max `544.4/546.192 us`，complete-product `715.2/722.804 us`，均
0 miss。它在 instrumented complete product 上已经跑过，所以按当前规则仍可进入正常
固件试听；但正常 build/flash、wet/dry、压力和用户听感没有 supplied evidence。Q 当前
在设备上也不构成对 D 的淘汰。

## 6. Block-rate rotation/modal bank

这条路径同样采用二维 `rho R(theta)`，但把每个模态的 `rho/sin/cos` 和输出标定明确
锁存在 32-sample block，9 个模态保持彼此独立。note 或未来 ratio 发生不连续变化时，
新频率最迟在下一 block 生效，原状态不 reset。mute 只关直接输出，状态继续接收公共
输入并推进。

它不追求历史 transfer-function parity，而要求每个单模态仍有一个主要窄峰。642 host
rows 全部通过，24 个 peak rows 均只有一个 significant peak；最大 static pole error
`0.560759216 cents`。generated-class p99/max `450.0/455.071 us`；complete-product
`717.1/725.071 us`，0 miss。

历史 p99 门禁在正常固件前停止了实验。此外完整产品日志显示生成后的 shared path 是
`/Wingie/mode_changed`，固件却查找 `/Wingie/left/mode_changed`，出现两次 not-found。
这不抹掉硬件运行证据，也不能被误写成听感失败；但在有意义的正常试听前，它是必须
显式处理的产品控制路径回归。当前没有该候选的 audible wet 或用户试听结论。

## 7. Normalized-lattice E

E 把历史 numerator 提到 9 模态分流之前，每通道共享计算 `x[n]-x[n-2]`；每个模态用
两段 normalized lattice 实现 all-pole denominator，再乘 `1/(c1*c2)`。因此固定系数时
它精确表达历史传递函数，同时让递归状态处在 normalized coordinates。系数跳变时状态
继续运行，但瞬态不要求逐 sample 等于历史 direct form。固定系数等价是结构恒等式；
E 的 601 个 complete-product host rows 被旧门禁跳过，不能写成完整 renderer parity 实测。

E 的代价是低频、长 Decay 时 `c1*c2` 很小，输出归一化会放大很小的内部状态。三个旧
host 标签的 normalized output max 为 `14.6763868`、`10.3700294`、`139.9256287`，
超过当时上限 8；但对应 state energy 都是 bounded，0/6141 violations，不能写成状态
发散。旧门禁因此跳过了 E 的 601 complete host rows。

E generated-class p99/max 为 `395.8/396.246 us`，0 miss；complete String 与 Cave
p99 分别 `719.5/719.6 us`，max `725.400/725.158 us`，均 0 miss。用户评价
`E 也非常好，而且音量明显比 R 要高。`，并确认 E/R 试听中的左右 wet 与 dry/thru
正常。E 有四个可归属压力批次，但不是完整模式矩阵；mute/unmute 仍不能标 PASS。

segmented observer 在构造时为五个 `8192 x uint32_t` histogram 申请约 160 KiB，因
连续 heap allocation 失败而 reboot loop。matching ELF 将栈定位到 `operator new` 和
`Wingie2::Wingie2`；这只使 segmented 测量无效，不能归因于 E DSP。

## 8. Normalized-lattice R

R 使用与 E 相同的 normalized lattice 和 reflection coefficients，但公共输入直接进入
lattice，输出 raw terminal tap；它没有 `1-z^-2` 输入差分，也没有 `1/(c1*c2)`。因此
R 和 E 有相同 pole 与 normalized 状态边界，却有意具有不同 DC、宽带响应、模态电平、
静态增益、激励与快速换频瞬态。

R 完成全部 601 host rows，旧状态为 `completed_with_failures`。6 个失败都来自 16 Hz、
indices 0/4/8、左右两边；有限长度 FFT 给出 `16.074198 Hz`，即 `+8.009839 cents`。
这只是旧 FFT estimate gate 的标签，不是物理听感跑调结论。

R generated-class p99/max 为 `370.6/371.113 us`，0 miss。正常固件已经运行；用户说
`听起来没问题`，并确认左右 wet 和 dry/thru 正常。旧 host gate 跳过了 complete-product
Task 8，新规则后没有补跑，所以 R 的 complete timing、segmented timing 和完整压力矩阵
均为未测；mute/unmute 也没有可归属 PASS。

## 结构与听感取舍

固定系数传递函数、状态坐标和演奏中的换频瞬态是三个不同问题。E 可以与历史方案有
相同固定系数 transfer function，但 normalized state 在跳变时的轨迹仍不同；Q、D、
modal 和 R 可以共享或接近同一 pole，却因输出投影、零点和归一化不同而有不同音量、
激励和谱形。Host 的 cents、T60、gain 或 peak 数量只能描述这些差异，不能给音色排序。

目前唯一直接的 E/R 听感比较是用户认为两者都好，并指出 E 明显比 R 响。Q 的结论是
“听起来没问题”。D 和 modal 没有正常固件试听。除此之外，不存在足以支持
“更自然”“更稳所以更好听”或八方案总排名的真机证据。

mute 语义也不完全相同。历史 direct form、block-rate direct form 和 Q/D 沿用历史
输出增益路径及 250 ms ASR；modal bank 与 E/R 的设计是 direct visible-output gate，
mute 时递归仍推进。`fi.tf2np` 的完整产品没有生成，不能把计划中的兼容性写成实测。

## 计算预算与完整产品时序

统一条件为 ESP32 240 MHz、44,100 Hz、32-sample block、100,000 measured blocks。
`--` 表示该阶段不存在或未测，不表示 0 成本。

| 路径 | Generated p99 / max | Generated misses | Complete p99 / max | Complete misses | 解释 |
| --- | ---: | ---: | ---: | ---: | --- |
| 历史 direct form | `739.5 / 801.817 us` | 1,308 | `715.9 / 724.921 us` | 0 | generated 超时；complete 含 I2S wait |
| Block-rate decay direct form | `419.1 / 424.071 us` | 0 | `456.3 / 469.642 us` | 0 | 旧时序门禁通过，物理 wet 失败 |
| 直接 `fi.tf2np` | -- | -- | -- | -- | 完整 Faust 图未生成 |
| Coupled Q | `544.7 / 546.517 us` | 0 | `714.6 / 723.625 us` | 0 | 旧 complete p99 FAIL；当前保留 |
| Coupled D | `544.4 / 546.192 us` | 0 | `715.2 / 722.804 us` | 0 | 旧 complete p99 FAIL；当前保留 |
| Rotation/modal bank | `450.0 / 455.071 us` | 0 | `717.1 / 725.071 us` | 0 | 旧 complete p99 FAIL；当前保留 |
| Normalized-lattice E | `395.8 / 396.246 us` | 0 | String `719.5 / 725.400 us`; Cave `719.6 / 725.158 us` | 0 / 0 | 旧 complete p99 FAIL；当前保留 |
| Normalized-lattice R | `370.6 / 371.113 us` | 0 | -- | -- | 旧 host gate 跳过 complete timing |

complete-product timer 从 blocking `i2s_read` 后开始，到 blocking
`i2s_write(..., portMAX_DELAY)` 后结束，包含 DSP、输入输出转换和等待 DMA/I2S 的时间。
这解释了多个不同递归都集中在约 `708..719 us`，但不能据此相减出 DSP CPU 成本。它仍
能回答观察期内是否 missed hard deadline；generated-class measurement 更接近候选 audio
class 成本，但又不包含完整固件路径。

## 当前真机证据与试听顺序

| 候选 | 完整候选硬件运行 | 正常固件 | Wet/dry | 压力 | 用户试听 | 仍缺 |
| --- | --- | --- | --- | --- | --- | --- |
| Q | 有，0 miss | 有；当前设备 | 未单独记录 | 未形成新的完整矩阵 | `我听起来没问题。` | wet/dry、系统压力、分模式与 mute 记录 |
| D | 有，0 miss | 未测 | 未测 | 未测 | 未测 | 全部正常固件阶段 |
| Modal bank | 有，0 miss | 未测 | 未测 | 未测 | 未测 | 先处理/核验 `mode_changed` 路径，再做正常试听 |
| E | 有，两个 complete captures 均 0 miss | 有 | 左右 wet、dry/thru 正常 | 四个可归属 batches，非完整矩阵 | `E 也非常好，而且音量明显比 R 要高。` | 完整模式压力与 mute/unmute |
| R | generated 与正常固件有；complete timing 未测 | 有 | 左右 wet、dry/thru 正常 | 完整矩阵未测/不可归属 | `听起来没问题` | complete timing、压力与 mute/unmute |

这张表只整理已发生的事实，不替用户决定下一台正常固件。Q、E、R 已有正常试听；D
和 modal 是尚未正常试听的保留候选。二者谁先试、是否重试 E/R 压力，以及最终选择
Q/E/R/D/modal/都不选，当前记录均无决定。

## 各类门禁的意义与边界

| 门禁 | 回答什么 | 不能回答什么 |
| --- | --- | --- |
| Toolchain / artifact identity | 编译器、library、源、generated C++、header 和镜像是不是指定批次；后续能否复核同一对象 | 不说明算法正确、镜像已刷入或声音可用 |
| 数学系数与 contraction | 反射系数是否在边界内，零输入状态能量是否按模型不增长，重复输入是否确定 | 不说明完整 Faust 图、浮点实现、UI 或声音一定正确 |
| Generated structure 与 UI/control path | 昂贵运算位于 sample loop 还是 block path；状态数、mute branch、UI path 和 alternate tuning 是否保留 | 不说明 ESP32 deadline、wet 或听感；modal 的 UI 回归正是在这一层发现 |
| Static response / pole / FFT / T60 / output bound | 固定条件下的中心频率、衰减、增益、主要峰、数值幅度和 E/R 响应差异 | 不说明快速手势瞬态或物理听感；R 的 16 Hz FFT 和 E 的 output bound 都不能单独作听感结论 |
| Dynamic host determinism | rapid note、frequency/ratio jump、decay jump、mute-state 下是否 finite、可重复、状态继续 | 不说明真实 MCU 调度、I2S、MIDI、wet 或演奏手感 |
| Generated-class deadline | 候选 audio class 在注册 stimulus 下的计算分布、输出 finite/varied 和 hard miss | 不包含完整产品路径，也不是正常固件试听 |
| Complete-product deadline | instrumented 产品在真实 ESP32 audio path 的端到端分布与观察期 hard miss | 因包含 blocking I2S wait，不能当纯 DSP CPU profile；0 miss 不等于有 wet |
| Image size / checksum / flash address | 镜像能否放入 app0、文件是否完整、写入是否限制在 `0x10000` | 不说明镜像已稳定运行、输出正确或设备当前仍是该镜像 |
| MIDI parser / callback pressure | 大批 MIDI 是否解析、触发 callback/marker，是否 reset 或 stall | counters 正常不保证 audio wet；block-rate direct form 已证明这一区别 |
| Wet/dry、mute、运行与用户试听 | 真实设备是否有目标 wet、对照 wet、dry/thru，mute 手感与用户对具体 exposure 的判断 | 一次确认不覆盖未试模式、长期压力、数值 parity、仓库安装或其他候选 |

当前只让“完整方案能否在硬件上跑”淘汰候选，是因为其余指标描述不同设计取舍，不能
替代真机。但这些门禁仍然有用：它们能指出 TF2NP 没有完整候选、block-rate direct
form 的 wet 是物理失败、modal 有 UI 回归、E 有输出归一化放大、R 缺 complete timing，
并精确限定下一次真机试用需要观察什么。

## Git 与实验边界

- `main` 产品源仍是历史 direct-form triplet；本文和 normalized-lattice 结果都是文档，
  没有安装任何候选 DSP。
- `fix/custom-normalized-lattice` 在 `beaa6f6` 保留 E/R 测试、生成结构检查和 segmented
  instrumentation；它是实验史，不是应整体 merge 的产品分支。
- `archive/tf2np-deadline-rejected-2026-07-11` 保存 block-rate decay direct form 与
  直接 TF2NP 审计；`archive/coupled-mode-filter` 保存 Q/D；
  `archive/block-rate-modal-bank-2026-07-12` 保存 modal bank。
- branch 是指向一段提交历史的 Git 引用；worktree 是把某个 branch 实际检出成可读写
  目录。同一 branch 通常同时只在一个 worktree 中检出，但 archive branch/tag 可以没有
  独立目录而仍可用 `git show` 审计。
- `/tmp` 中的 JSON、serial log、ELF、固件和 manifests 是原始外部证据，不提交 main；
  durable 报告记录关键数值、hash 和证据边界。当前设备镜像与 main 产品源必须分开描述。

## 证据索引

| 范围 | Durable summary | 详细实验证据 |
| --- | --- | --- |
| Block-rate decay / direct TF2NP | `docs/superpowers/results/2026-07-10-mode-filter-tf2np-results.md` | `archive/tf2np-deadline-rejected-2026-07-11` 的 deadline result |
| Coupled Q / D | `docs/superpowers/results/2026-07-11-coupled-mode-filter-results.md` | `archive/coupled-mode-filter` result/plan 与留存 Q/D JSON |
| Block-rate modal bank | `docs/superpowers/results/2026-07-11-block-rate-modal-bank-results.md` | `archive/block-rate-modal-bank-2026-07-12` result/plan |
| Normalized-lattice E / R | `docs/superpowers/results/2026-07-12-mode-filter-custom-normalized-lattice-results.md` | `fix/custom-normalized-lattice`、冻结 manifests、host/ESP32 JSON 与 logs |
