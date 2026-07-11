# Wingie2 Block-Rate Modal Bank 设计

## 状态与边界

本设计是新的 mode-filter 研究路线，不恢复或修改以下已拒绝计划：

- `docs/superpowers/plans/2026-07-10-mode-filter-tf2np-stability.md`
- `archive/coupled-mode-filter:docs/superpowers/plans/2026-07-11-coupled-mode-filter.md`

TF2NP 与 coupled rotation-damping 的结论分别记录在：

- `docs/superpowers/results/2026-07-10-mode-filter-tf2np-results.md`
- `docs/superpowers/results/2026-07-11-coupled-mode-filter-results.md`

固定工具链与安全边界继续有效：Faust `2.59.6`、ESP32 Arduino Core `2.0.4`、
不增加控制侧补偿、平滑、rate limit、reset、crossfade、fallback、自动增益或新的
时间常数。真机只写 app0 `0x10000`；每个阶段结束后恢复历史 app0、完整 verify，
并比较 `0x140000` readback。任何物理听感 PASS 必须由用户明确确认。

## 目标

用一个在实时、不连续换音与未来实时 ratio 变化下仍保持有界的 9 模态 bank，
替换当前快速改变 `pm.modeFilter` 系数时会令目标侧 wet 消失的 direct-form 实现。

本轮优先验证低成本、无模态耦合的 block-rate rotation-damping bank。模态耦合是
允许的内部手段，不是修复成立的必要条件。只有独立 bank 通过全部机器门禁后，
才允许单独评估固定稀疏耦合候选。

## 产品控制语义

每个通道的 bank 接收：

- 9 个独立目标频率；
- 一个通道级 Decay；
- 9 个直接输出 mute；
- 一个公共音频输入。

频率生成层负责 Poly、String、Bar 与现有其他 mode，bank 不知道这些 mode 的比例
规则。未来的用户自定义模式只需把 `fundamental * ratio[0..8]` 作为同一组 9 个
频率输入；本轮不实现该 UI、存储或 MIDI 映射。

目标频率具有以下语义：

- 每项独立 clip 到闭区间 `[16 Hz, 16 kHz]`；
- 不要求排序或互异，允许相同、交叉和任意无序频率；
- 达到任一边界后仍可听、仍运行状态；
- note 与未来 ratio 均允许在演奏中实时、不连续改变；
- 新频率在下一个 32-sample audio block 生效，44.1 kHz 下最迟约 `0.73 ms`；
- 频率跳变不清除、不复制状态，原状态立即在新系数下继续。

用户 mute 只关闭对应模态的直接输出。被 mute 模态仍接收公共输入并运行内部
状态；若未来采用耦合，它仍可通过耦合影响其他可听模态。

每通道只有一个 Decay。它决定整个 bank 的总能量衰减；耦合可改变单个模态的
实际尾音，但不得破坏总系统的有界性。现有 Decay 的产品范围 `0.1..10 s` 保持。

所有模态接收同一通道输入。不增加随 mode、频率或 mute 变化的隐藏输入分配。
现有 `resonator_input_gain`、4 kHz 输入低通、mode-change envelope、wet/dry、音量、
pre/post clip gain 和非线性输出链均保持在 bank 外部。

## 主候选：全系数 Block-Rate Rotation Bank

每个模态保存二维状态 `z_i`：

```text
z_i[n + 1] = rho * R(theta_i) * z_i[n] + B * x[n]
y_i[n]     = Q * z_i[n]
```

`R(theta_i)` 是目标频率对应的二维旋转；`rho < 1` 由通道 Decay 决定。任一时刻的
零输入状态变换都是收缩乘旋转，因此稳定性不依赖频率连续、排序或互异。

每个 32-sample block 只准备一次并锁存每个模态的：

- `sin(theta_i)`；
- `cos(theta_i)`；
- 任何依赖该模态频率的输出标定系数。

Decay 是通道级控制，因此 `rho` 和任何只依赖 `rho` 的输出标定每通道每 block
只准备一次，再扇出给该通道的 9 个模态；不得为了 Faust 图连接而实际重复复杂
计算。

逐 sample 路径只执行二维状态递推、公共输入注入、直接输出 mute 和求和。主候选
采用成本最低的 quadrature 输出投影，不加入 Candidate D 的一拍差分状态。

主候选保持 9 个模态相互独立。若其全部机器门禁通过，可另行评估固定稀疏耦合：
耦合只能在状态递推内部交换能量，必须保持整体收缩，不能改变 9 频率接口、
Decay、公共输入或 mute 语义，也不能增加新的面板/MIDI 参数。

## 声音约束

当只开启一个模态的直接输出时，该模态必须保持一个主要窄峰，而不能以明显的
comb/waveguide 次峰群换取稳定或算力。其主要峰中心相对 clip 后目标频率的误差
必须不超过 `+/-5 cents`。

当多个模态共同输出时，若采用耦合，允许峰值移动、分裂或合并。耦合强度不是
本轮的新控制参数；首个可用结构必须是固定、确定且可重复的。

本轮允许可听的静态响应变化，但不允许牺牲单模态的主要窄峰身份。候选的物理
音高、Decay、gain、动态、慢速/快速换频、dry/thru 和左右 wet 最终都由真机 A/B
决定；host 指标不能替代物理听感确认。

## 候选顺序

候选严格按以下顺序推进：

1. 全系数 block-rate、相互独立的 rotation-damping bank。
2. 仅当候选 1 因 ESP32 deadline 失败时，另行设计 custom normalized-lattice bank；
   它不得调用或恢复旧 `fi.tf2np` 实施路线。
3. 仅当候选 1 已通过全部机器门禁时，才可选择评估固定稀疏耦合候选。

不把全局 dense state-space、FDN、comb 或 waveguide bank 作为本轮候选。前者风险是
运算量和单模态可辨识性，后者不满足一个模态约等于一个主要窄峰的硬约束。

## 验证门禁

### 1. 数学与 Host

覆盖至少：

- `16 Hz` 与 `16 kHz` 两个边界；
- Decay `0.1 s` 与 `10 s`；
- 9 个相同频率、交叉频率、逆序和任意无序频率；
- slow note、rapid note 与模拟 future ratio 的逐 block 不连续变化；
- 单模态 impulse/static matrix 与 9 模态动态输入。

要求：

- 所有状态与输出有限、有界；
- 零输入总状态能量不增长；
- 快速改变系数后尾部仍有活动，证明状态没有被清除；
- 每个单模态只有一个主要窄峰，中心误差 `<= +/-5 cents`；
- 重复输入与初始状态产生确定性相同输出。

主要窄峰的自动判定方法必须在实施计划中固定：目标峰为全频段最大局部峰，其他
局部峰相对它的允许幅度和搜索窗不能在看到候选结果后再调整。物理听感仍是最后
门禁。

### 2. Faust 2.59.6 生成结构

必须从完整产品生成代码证明：

- `pow/sin/cos` 只位于每 32 samples 执行一次的系数准备路径；
- 逐 sample 状态 loop 不包含 `pow/sin/cos/sqrt/tf2np/nlf2`；
- 18 个状态仍逐 sample 更新；
- 两个通道共 18 组频率相关 `sin/cos` 系数只按 block 更新；
- 通道级 Decay/rho 只准备两组，任何频率相关输出标定与对应模态同频率更新；
- alternate tuning 完整生成；
- DSP、generated C++ 与 header 来自同一次 Faust 2.59.6 生成批次。

仅检查源 Faust 表达式不构成通过；必须检查最终 generated user section 和完整产品
renderer。

### 3. ESP32 Deadline

先测 generated class，再测完整产品路径。每个分布使用 100,000 blocks、240 MHz、
44.1 kHz、32-sample block，并在测量中持续改变 note/ratio stimulus。硬门禁为：

- p99 `<= 580.5 us`；
- max `< 725.6 us`；
- deadline misses `= 0`。

任一机器门禁失败立即淘汰该候选，不构建 normal/diagnostic candidate firmware。

### 4. 真机压力与听感

只有全部机器门禁通过才进入真机。左右通道分别覆盖 String、Poly、Bar，以及直接
驱动 9 个任意频率输入的 future-ratio 等价 stimulus。每个压力批次检查 MIDI
parsed/callback、marker、reset/stall、目标 wet、对照 wet 和 dry/thru。

每个硬件阶段：

1. 写入并 verify candidate app0，地址只能是 `0x10000`；
2. 完成该阶段所有机器记录和用户听感确认；
3. 写回历史 app0 并完整 verify；
4. readback `0x10000..0x14ffff` 共 `0x140000` bytes 并逐字节比较；
5. 确认 MIDI `1/2/3`、A3 `440`、Poly/Poly、standard tuning；
6. 等待用户明确确认恢复后的左右 wet 正常。

任何物理 PASS 必须记录用户原话。数值稳定、deadline 通过或 agent 判断均不能替代
听感确认。

## 产品与 Git 边界

研究在新的独立 worktree/branch 进行。候选源、generated artifacts、固件和测量
数据在机器门禁通过前保持在仓库外。只有一个候选通过全部机器、压力与用户 A/B
门禁后，才允许修改 `Wingie2.dsp` 并同批更新 `Wingie2/Wingie2.cpp`、
`Wingie2/Wingie2.h`。

若所有候选均被拒绝，完整实验历史保留在 archive branch/tag，`main` 只接收一个
精炼结论提交，不合并候选专用测试、计划修订或 benchmark 基础设施。
