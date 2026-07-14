# Mode Filter 最终决策

## 决定

Wingie2 选择 **全系数 block-rate rotation-damping modal bank** 替换
`pm.modeFilter`。产品实现提交为 `a22192a`。

每个声道包含 9 个相互独立的二维共鸣器：

```text
z_i[n + 1] = rho * R(theta_i) * z_i[n] + B * x[n]
y_i[n]     = (2 / rho) * q_i[n]
```

状态逐 sample 更新；`rho`、`sin(theta_i)`、`cos(theta_i)` 和输出标定每
32 samples 更新一次。目标频率限制在 `[16, 16000] Hz`，换频保留状态。

## 选择依据

### 1. 直接解决已知稳定性问题

旧 `pm.modeFilter` 使用 direct-form 延迟状态。高速换频时，旧状态会被新的分母系数
重新解释；真机已在 String、长 Decay、快速 MIDI 换音下复现 wet/resonator 停止。

rotation bank 在零输入时满足：

```text
||z_i[n + 1]|| = rho * ||z_i[n]||,  0 < rho < 1
```

`R(theta_i)` 只旋转状态，不改变能量。频率可以任意跳变、重复、交叉或逆序，收缩条件
不变；不需要 reset、crossfade、pitch smoothing 或 NaN fallback。

### 2. 适合后续 Auto Gain/Decay Control

每个模态都有明确、相位无关的能量：

```text
energy_i = p_i^2 + q_i^2
```

反啸叫控制可以只降低当前模态的 excitation gain，并令
`rho_effective_i <= rho_user`。控制器因此能直接观察和约束每个共鸣器，同时保持原有
收缩证明，不需要从最终 wet sum 反推是哪一个模态失控。

### 3. 适合未来独立 ratio

未来只需令 `theta_i` 来自 `fundamental * ratio_i`。每个模态已经接收独立目标频率，
ratio 的实时跳变不会改变 pole radius 或稳定条件。自由 ratio 的 UI mode 另行设计，
不属于本次实现。

### 4. 产品成本可接受

冻结候选的 generated-class ESP32 测量为 p99 `450.0 us`、max `455.071 us`、
0 deadline miss。最终产品使用 ESP32 Core 2.0.4 完整编译；程序占用 90%，动态内存
占用 15%。

## 未选择的方案

- **Block-rate direct form**：只降低 decay `pow` 成本，没有改变导致高速换频故障的
  direct-form 状态结构。
- **直接 `fi.tf2np`**：固定 Faust 2.59.6 下无法生成完整 alternate-tuning 产品图。
- **Coupled Q**：使用相同的 `rho R(theta)` 核心，但没有把全部频率系数明确降到
  block rate，generated-class p99 为 `544.7 us`。
- **Coupled D**：在 Q 后增加一拍差分状态，改变低频和增益，却不增加稳定性或未来控制
  能力。
- **Normalized-lattice E**：固定系数时最接近历史 transfer function，但低频长 Decay
  的 `1/(c1*c2)` 输出归一化曾放大到 `139.9256`，不适合作为反啸叫控制基础。
- **Normalized-lattice R**：稳定且计算更低，但 Decay、内部能量、excitation 和 audible
  output 的关系不如 rotation bank 直接，使逐模态 Auto Gain/Decay 标定更复杂。

历史 transfer-function 等价不是核心目标；稳定性、逐模态控制结构和后续扩展优先。

## 最小验证

- 固定 Faust 2.59.6 完整生成，alternate tuning 保留。
- 生成结构为 18 个二维状态、36 个共同乘 `rho` 的更新、2 个 block-rate decay
  `pow`，没有 `sqrt/tf2np/nlf2`。
- 边界、重复、交叉、逆序和 ratio-jump contraction 测试全部 finite，状态能量下降，
  尾音未 reset。
- 最终产品 standard/alternate 下的 rapid notes、frequency jump、mode change、Decay
  step 和 mute-state 均 finite、active。
- 左右 `mode_changed`、Decay、mute 和 Cave 参数路径与固件一致。
- ESP32 Core 2.0.4 全固件编译通过，设备 app0 digest verify 通过。
- 用户已确认最终实现的基础真机试听正常。
- 当前固件已完成左右各 10,000 对 String、Decay 10 s、1 ms interval 的 MIDI
  stimulus；用户确认：`左右均有 wet，压力测试通过`。

## 后续边界

下一项工作是反啸叫 Auto Gain/Decay Control。是否默认开启以及是否不给用户 bypass，
必须由同一固件的 on/off 真机 A/B 决定。自由 ratio 等新 UI mode 设计后再讨论。
