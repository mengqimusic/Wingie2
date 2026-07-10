# Wingie2 TF2NP Mode Filter Stability 设计

## 目标

从 resonator 滤波器算法本身修复高速 MIDI 换音后单侧 wet 永久无声的问题。第一候选使用 Faust `fi.tf2np` 的 protected normalized-ladder biquad，保持现有 MIDI/control 语义和静态 modal 参数映射。

候选只有在静态声音、ESP32 CPU deadline、长时间真机稳定性和用户 A/B 听感全部通过后才能进入产品固件。

## 已确认边界

左右两侧分别在 String mode 用 10,000 对变音 Note On/Off 复现 wet 消失。每次均满足：

- `parsed == note_on + note_off == 20,000`；
- parser errors 为 0；
- 故障后 callback 与 Faust `note0/note1` 继续更新；
- dry/thru 和未受压的另一侧 wet 正常。

因此故障边界位于被测侧的 wet/resonator 状态。快速、不连续地改变同一组高 Q `pm.modeFilter` 的频率系数是已确认的触发路径。现有诊断没有证明内部最终是 NaN、Inf、极大有限值还是恒定输出。

## 非目标

本修复不在 MIDI 或 control 路径加入：

- rate limit 或丢弃消息；
- pitch smoothing、portamento 或 debounce；
- note change 时的滤波器 reset；
- 新的 mute envelope、timeout、自动恢复或运行时 fallback。

所有合法 MIDI pitch 必须继续即时写入 DSP。稳定性由递归滤波器结构负责。

## 候选算法

当前 `pm.modeFilter` 等价于：

```faust
fi.tf2(1, 0, -1, a1, a2) * gain
with {
  w = 2 * ma.PI * freq / ma.SR;
  rho = pow(0.001, 1.0 / (t60 * ma.SR));
  a1 = -2 * rho * cos(w);
  a2 = rho * rho;
};
```

候选 `stableModeFilter` 保持相同的 numerator、pole radius、frequency、T60 和 gain，只把 direct-form `fi.tf2` 替换为 `fi.tf2np`：

```faust
stableModeFilter(freq, t60, gain) = fi.tf2np(1, 0, -1, a1, a2) * gain
with {
  w = 2 * ma.PI * freq / ma.SR;
  rho = pow(0.001, 1.0 / (t60 * ma.SR));
  a1 = -2 * rho * cos(w);
  a2 = rho * rho;
};
```

`tf2np` 把 denominator 转成 normalized-ladder reflection coefficients，并把它们投影到定义的稳定区域。它比 direct-form `tf2` 更适合时变系数，但会引入 `sqrt(1-s*s)` 和额外的 scattering/tap 运算。

Wingie2 的 `t60` 包含 `env_mode_change` 和 smoothed decay，生成器可能把 reflection coefficient 和 sqrt 保留在逐 sample 循环。因此不能根据公式静态计数判断性能，必须检查 Faust 生成代码并在 ESP32 上测量。

## 候选顺序

1. 直接使用标准库 `fi.tf2np`。
2. 只有直接版本未通过 CPU 门禁时，停止并记录结果，再设计 Wingie 专用 normalized-ladder mode filter，通过共享 `rho`、`a2/s2` 等值减少计算。
3. 只有 normalized-ladder 路线仍不可行时，才另行设计 coupled-form 或 state-variable resonator；该方案不属于本 spec。

任何失败都不会触发控制侧补偿或静默切换算法。

## Faust 工具链门禁

当前生成文件声明 Faust 2.59.6，项目要求 DSP 变更时同时更新生成的 `Wingie2.cpp` 与 `Wingie2.h`。实施前必须：

1. 获取并记录 Faust 2.59.6 compiler 和标准库版本。
2. 在临时目录用未修改的 `Wingie2.dsp` 重新生成。
3. 对比当前 tracked generated files，确认差异为空或只来自可解释、经审核的生成环境元数据。
4. 用 ESP32 Arduino Core 2.0.4 编译 parity 产物。

若 parity 失败，先解决或固定生成工具链。本 spec 不允许用当前 Faust 2.85.5 产生的大范围 architecture/library churn 掩盖单一滤波器修改。

## 静态 DSP 验收

旧 `pm.modeFilter` 与候选 `stableModeFilter` 使用相同输入，覆盖：

- Poly、String 和 Bar mode；
- MIDI notes 24、36、60、84、96；
- mode indices 0、4、8；
- decay 0.1、5、10 seconds；
- standard tuning 和一个 alternate tuning 配置；
- 非零 impulse、noise 和 sine 输入。

对每个未被 16 kHz cap mute 的 mode 测量：

- modal center frequency：误差不超过 1 cent；
- T60：相对误差不超过 2%；
- peak gain：误差不超过 0.25 dB；
- 输出全程 finite，没有恒定 DC 锁死。

不要求两个结构逐 sample null-match。数值门禁通过后仍必须由用户在真机比较静态音高、衰减、动态响应和快速换音；任何不可接受的可听差异都会拒绝候选。

## ESP32 性能门禁

建立独立于产品生成文件的 ESP32 compute benchmark。它必须使用：

- ESP32 Arduino Core 2.0.4；
- 与产品相同的 `mydsp`、single precision、44.1 kHz 和 32-sample block；
- 非零 deterministic input；
- baseline 和 candidate 相同的 UI 参数变化序列；
- Xtensa cycle counter 或等价的 microsecond timer，只包围 `compute(32)`。

每个版本预热后至少测量 100,000 个 blocks，并报告 median、p95、p99 和 max。32 samples / 44.1 kHz 的 deadline 是 725.6 microseconds：

- 任一 measured `compute(32)` 不得达到或超过 725.6 microseconds；
- p99 不得超过 580.5 microseconds，即 deadline 的 80%；
- benchmark 期间不得 watchdog reset；
- candidate 的程序和 RAM 使用量必须低于 ESP32 partition/heap 上限。

Host Faust benchmark 只用于解释 operation cost，不能替代 Xtensa 测量。

## 真机稳定性矩阵

诊断时继续使用持续 serial session 和固定大小 MIDI counters，压力期间不逐消息打印。decay 设为 10 seconds，pitch 按 36 到 95 循环：

- String mode：channel 1 和 2 各 100,000 Note On/Off pairs；
- Poly mode：channel 1 和 2 各 10,000 pairs；
- Bar mode：channel 1 和 2 各 10,000 pairs。

每批结束后先读取计数，再向目标侧和对照侧各发一个低速 marker。每批必须满足：

- sent、parsed 和 callback counts 完整一致；
- parser errors 为 0；
- 目标 DSP pitch 参数继续更新；
- 左右 wet、dry/thru 都保持响应；
- 没有 boot、watchdog、audio dropout 或 channel-specific silence。

若任一批失败，停止后续批次并保留现场 snapshot，不用更多修改覆盖故障状态。

## Flash 与 A/B 边界

候选验证只写当前 app0 partition，不写 bootloader、partition table、otadata 或 NVS。每个 baseline/candidate app0 在写入前记录 size、SHA-256 和 ESP32 image validation；写入后对同一 offset verify。

baseline 与 candidate 切换时分别记录启动设置和听感。测试结束前必须明确确认设备最终运行哪一个已校验 image，不默认把候选留在设备上。

## 文件与提交边界

- Modify: `Wingie2.dsp`，新增 `stableModeFilter` 并替换 `pm.modeFilter` 调用。
- Regenerate together: `Wingie2/Wingie2.cpp`、`Wingie2/Wingie2.h`。
- Create: `tests/dsp/`，存放隔离的 old/new mode-filter Faust test DSP、deterministic renderer 和静态指标分析脚本。
- Create: `tests/esp32/wingie2_dsp_benchmark/`，存放只测 `mydsp::compute(32)` 的 ESP32 benchmark sketch；baseline/candidate DSP headers 由测试命令生成到临时目录，不提交生成 benchmark artifacts。
- Create: `docs/superpowers/results/2026-07-10-mode-filter-tf2np-results.md`，记录 parity、static metrics、CPU distributions、stress matrix 和 A/B 结论。

Benchmark/验证工具、DSP source/generated output、硬件结果分别提交。Tap Sequencer 修复不与任何 Faust 生成文件混在同一提交。

## 接受条件

只有下列条件全部满足，直接 `TF2NP` 候选才可进入产品分支：

1. Faust 2.59.6 regeneration parity 可解释且通过 Core 2.0.4 构建。
2. 所有静态频率、T60、gain 和 finite 指标通过。
3. ESP32 compute deadline 和资源门禁通过。
4. 左右完整真机压力矩阵通过。
5. 用户真机 A/B 听感通过。

否则保留结果、恢复 baseline，并回到下一算法候选的独立设计阶段。
