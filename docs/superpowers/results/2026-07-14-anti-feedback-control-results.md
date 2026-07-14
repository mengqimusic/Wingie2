# 反啸叫能量控制阶段结果

## 当前状态

逐模态 Auto Gain/Decay 候选已实现、生成、编译并烧入真机。当前设备默认开启候选，
但产品门限与最终是否提供用户 bypass 尚未决定。自由 ratio 与新 UI mode 均未改动。

## 门限分类

### 硬边界

- audio block 为 32 samples，44.1 kHz 下完整 block 时间为 725.624 microseconds；
- 完整 audio callback 必须保持 0 deadline miss；
- 所有状态与输出必须 finite；
- 所有时刻 `0 <= input_gain <= 1`；
- 所有时刻 `0 < rho_effective <= rho_user < 1`；
- controller inactive 时不得改变 modal bank 行为；
- 一个模态或声道触发不得改变其他模态或另一声道。

### 当前真机候选

- `anti_feedback_energy_limit = 1`；
- `anti_feedback_rho_guard = 0.998435`，在 44.1 kHz 下约等于 `T60 = 0.1 s`；
- `anti_feedback_enabled = 1`；
- diagnostic 固件通过 USB serial `0`/`1` 在同一镜像中关闭/开启。

这些值用于当前真机 A/B，不是冻结的产品安全边界。energy/rho slider 的 min、max 和
step 只服务诊断 API 元数据，也不构成产品验收门限。

### 不再沿用的门限

旧 mode-filter 实验曾使用 compute p99 `<= 580.5 us`，即 block deadline 的 80%。
该数值来自 2026-07-10 TF2NP 实验，不在反啸叫计划 `e47c143` 中，也没有由当前
完整产品 jitter 测量推出。本功能只报告 generated-class 分布；最终 timing 结论必须
来自完整 audio callback 的 0-miss 测量。

## 控制实现

每个模态独立保存 `q/p`、当前 block peak、input gain 与 effective rho。block 边界使用
上一 block 的峰值：

```text
overload   = max(1, previous_peak / energy_limit)
input_gain = enabled ? 1 / overload : 1
rho        = enabled && overload > 1 ? min(rho_user, rho_guard) : rho_user
```

当前 block 内只累计 `q^2 + p^2` 的峰值，供下一 block 使用。mute 仍只作用于 audible
projection，不关闭 detector，也不 reset modal state。输出标定继续使用 `rho_user`，因此
controller inactive 时保留原 modal-bank projection。

## Host 结果

独立 reference contract 覆盖：

- previous-block peak 时序；
- gain/rho 数学边界与 finite；
- inactive 与 bypass 等价；
- 模态隔离；
- 无 reset 的 recovery；
- mute 不停 detector。

完整产品 renderer 支持 normal、transient、sustained narrowband、rapid notes、mode
changes、feedback 与 isolation，并把 source gain、feedback gain 和 feedback delay 作为
显式 stimulus 参数。普通场景观察 10 seconds；feedback 场景使用 10 seconds feedback
加 10 seconds recovery，对应产品最长 Decay 的一个完整 T60 观察窗。

默认候选 `energy_limit = 1`、`rho_guard = 0.998435` 的 on/off 结果：

- normal、transient 与 mode changes：双声道 bit-exact，controller 未触发；
- rapid notes 压力场景：左声道 bit-exact；右声道触发，difference peak `0.015979`、
  difference RMS `0.003129`；该 stimulus 每 32 samples 改一次 note，是压力测试而非
  正常 MIDI 演奏；
- sustained narrowband：双声道触发，off RMS 约 `0.45148`，on RMS 约 `0.10314`；
- feedback（source gain `1`、loop gain `1`、delay `64` samples）：off RMS
  `0.41725`、on RMS `0.05117`；recovery tail off RMS `0.37958`、on RMS `0.01512`；
- isolation：受测左声道触发，未受测右声道保持 bit-exact。

产品 analyzer 对 bit-exact transparency、持续输入触发、feedback recovery 和声道隔离
执行定性 gate，不引入额外数值门限。rapid notes 的已知触发只记录，不伪装成通过。

reference renderer 与 Python reference 的比较容差为
`1e-5 * max(1, abs(reference))`。这是 single-precision generated-code 回归容差，不是
声音或产品门限。

## ESP32 结果

固定工具链：Faust 2.59.6、ESP32 Arduino Core 2.0.4-cn、240 MHz、44.1 kHz、
32-sample block。

generated `mydsp::compute(32)` 各测量 100,000 blocks：

| Stimulus | Median | p95 | p99 | Max | Misses | Non-finite |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Controller inactive target | 642.7 us | 643.3 us | 643.5 us | 644.071 us | 0 | 0 |
| Single-mode active target | 643.9 us | 645.2 us | 645.4 us | 647.875 us | 0 | 0 |
| 18-mode active target | 635.8 us | 639.6 us | 639.9 us | 642.229 us | 0 | 0 |

这些结果证明三种 generated compute stimulus 均未达到 725.624-us block deadline；它们
没有测量 I2S read/write、sample conversion、control task 或完整 audio callback，因此不能
单独关闭 product timing 门禁。

diagnostic 产品固件编译结果：

- program storage：1,192,621 / 1,310,720 bytes（90%）；
- global variables：50,740 / 327,680 bytes（15%）；
- app0 SHA-256：`c7773da4d11f06047fec7f9873287816706e6ff0395ec3750b93770ae19cdb06`；
- ESP32 checksum 与 validation hash 有效；
- 只写 app0 `0x10000` 并 verify digest，未改 bootloader、partition table、otadata 或 NVS。

## 真机状态

diagnostic app0 已烧入。串口回读确认默认参数为 enabled `1`、energy limit `1`、rho guard
`0.998435`；同一固件的关闭与重新开启均已回读确认。
