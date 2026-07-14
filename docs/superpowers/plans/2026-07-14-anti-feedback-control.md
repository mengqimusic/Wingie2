# 反啸叫控制实施计划

## 决策状态

本计划只设计基于 modal-bank 状态能量的 Auto Gain/Decay Control。自由 ratio 和
新 UI mode 不在本计划内。

推荐先做成 **默认开启、引擎可 bypass、暂不提供面板开关** 的产品候选。
是否最终成为用户不可关闭的默认功能，必须在同一固件的 on/off 真机 A/B 后由用户确认，
不能在实现前锁定。即使最终没有用户开关，开发和诊断版本仍保留 bypass。

## 功能定义

这不是识别麦克风啸叫频率的 detector，而是每个共鸣器独立的能量保护层。它限制
acoustic feedback、持续输入或其他原因造成的 modal energy runaway，不猜测能量来源。

每个模态已有二维状态 `p_i/q_i`：

```text
energy_i = p_i^2 + q_i^2
```

状态能量不依赖相位或中心频率，适合作为所有模态共用同一阈值的检测量。控制器不得
读取最终 wet sum、cubic 输出或其他模态来决定当前模态的行为。

## 首个控制候选

以 32-sample block 为控制周期，每个模态记录本 block 的 `peak_energy_i`，并在下一
block 使用：

```text
overload_i   = max(1, peak_energy_i / energy_limit)
input_gain_i = 1 / overload_i
rho_i        = min(rho_user, rho_guard)  when overload_i > 1
               rho_user                  otherwise
```

- `input_gain_i` 只衰减当前模态的新 excitation，不提高增益。
- `rho_guard < 1` 对应一个明确的 emergency Decay，只在超限时缩短尾音。
- `rho_i <= rho_user < 1`，因此控制器不会破坏 modal bank 的收缩证明。
- 低于阈值时 `input_gain_i = 1` 且 `rho_i = rho_user`，控制器应与未安装版本等价。
- mute 不关闭 detector；被 mute 模态仍受保护，避免 unmute 时释放积累的高能量。
- 首版不加入 release、hold、hysteresis、频率权重或跨模态联动。若真机证明存在可听
  chatter，再单独决定是否增加一个最小状态，而不是预先叠加控制层。

`energy_limit` 和 `rho_guard` 不在计划中猜数值。它们先作为引擎/诊断参数，通过完整
产品 render 和真机 A/B 确定；产品锁定后再决定是否保留为可映射参数。

## 默认开启与不可关闭

“默认开启”和“不可关闭”是两个不同决定：

### 默认开启

推荐。候选固件启动时 enable 为 1，因为只有默认路径才能充分暴露误触发、动态压缩和
CPU 风险；默认关闭会让保护层长期处于未验证状态。

### 用户不可关闭

暂不推荐立即锁定。Auto Decay 会改变 Decay 旋钮与实际尾音之间的关系；只要它进入
正常演奏范围，就是乐器行为而不只是安全边界。永久不可关闭会让同一手势的结果依赖
不可见的内部能量状态，也会失去现场规避误判的出口。

只有同时满足以下条件，才建议产品不提供用户 bypass：

1. 正常演奏、强瞬态和最长 Decay 下控制器不触发，on/off 输出在未触发区等价；
2. 可重复的真实啸叫场景中，开启版本始终比关闭版本更可用，没有不可接受的抽吸、
   突然截尾或音高相关偏差；
3. 参数锁定后仍通过 ESP32 deadline 和长时间运行；
4. 用户明确确认该边界属于设备保护，而不是希望保留的反馈演奏区域。

若第 1 或第 2 条不成立，应保留用户可达的 bypass；具体映射等待后续 UI 决策，不在
本计划中占用现有 mode 或控制手势。

## 实施步骤

1. 建立独立 reference model，记录 off/on 时每模态 energy、input gain 和 effective
   Decay；先验证控制方程和收缩边界，不修改产品。
2. 用完整产品 renderer 构造真实 feedback loop gain sweep、持续窄带输入、强瞬态、
   rapid notes、mode changes 和最长 Decay。只测无法从方程推出的触发、恢复与相互影响。
3. 在 `Wingie2.dsp` 中让 modal core 同时提供 audible projection 和能量；添加每模态
   block peak、gain/Decay control 与一个默认开启的 engine bypass。
4. 固定 Faust 2.59.6 同批生成 C++，检查逐 sample 路径没有新增复杂函数，控制计算只在
   block 边界运行，18 个模态仍相互独立。
5. 在 ESP32 Core 2.0.4 上编译并测 controller inactive、单模态 active、18 模态 active
   三种计算路径；要求 0 deadline miss。
6. 构建同一固件可切换的 on/off A/B。真机分别覆盖正常演奏和受控 feedback，记录触发
   模态、最大衰减、恢复行为，并由用户判断声音和手感。
7. 用户决定最终产品是默认开启且可关闭，还是默认开启且无用户开关；随后才冻结参数、
   Preferences/MIDI 映射和文档。

## 必要验证

- 数学：所有时刻 `0 <= input_gain_i <= 1`、`0 < rho_i <= rho_user < 1`，全部值 finite。
- Inactive parity：18 个 controller 都未触发时，产品输出与 modal-bank 基线一致。
- Isolation：一个模态超限不能降低其他模态或另一个声道的 gain/Decay。
- Recovery：移除 feedback 后能量回到阈值以下，下一 block 恢复用户 Decay，不 reset 状态。
- Product timing：controller 最坏路径持续运行时仍无 audio deadline miss。
- Hardware A/B：正常演奏透明性和真实 feedback 抑制分别由用户确认，不能互相代替。

## 提交边界

reference/host contract、DSP 实现与生成物、ESP32 结果、真机/default 决策分别提交。
未完成真机 A/B 前，不把“不可关闭”写进产品行为或 Preferences migration。
