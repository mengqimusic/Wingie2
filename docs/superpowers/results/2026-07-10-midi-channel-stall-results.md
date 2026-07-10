# Wingie2 Tap Sequencer 与 MIDI 单通道停止响应结果

## 结论

1. Tap sequencer 超过 12 个音的根因已确认：`seq[2][12]` 的写入位置和长度都没有上限，录入第 13 个音时发生越界写，播放时又把越界的 4 bytes 当作 `int` 音符读取。
2. MIDI 故障已在左右两侧分别复现。MIDI 消息、parser callback 和 Faust pitch 参数在故障后仍正常更新；停止的是被压力测试一侧的 wet/resonator 路径，dry/thru 和另一侧 wet 仍工作。
3. 真机上已确认的充分触发条件是：在 String mode 下，以约 1 kHz 请求发送速率连续改变同一侧 `note0`，从而快速改变该侧 9 个 `pm.modeFilter` 的递归滤波器系数。内部状态究竟变成 NaN/Inf、极大有限值还是恒定输出，现有诊断固件没有直接观测，不能进一步断言。

## Tap Sequencer

相关状态为：

```cpp
int seq[2][12], seqLen[2], playHeadPos[2], writeHeadPos[2];
```

首音写入 `seq[ch][0]`。之后每个音先执行 `writeHeadPos[ch] += 1` 和 `seqLen[ch] += 1`，再写 `seq[ch][writeHeadPos[ch]]`，但没有检查 `< 12`。播放端同样按没有上限的 `seqLen[ch]` 读取。

Core 2.0.4 正常构建 ELF 的相邻布局为：

| Symbol | Address | Size |
|---|---:|---:|
| `seq` | `0x3ffc38ac` | `0x60` |
| `threshChanged` | `0x3ffc390c` | `0x02` |
| `trigged` | `0x3ffc390e` | `0x02` |
| `trig` | `0x3ffc3910` | `0x02` |

因此：

- `seq[0][12]` 与 `seq[1][0]` 是同一地址，会破坏右侧序列的首音；这不等于每次都会产生可听的串扰。
- `seq[1][12]` 与 `threshChanged[0..1]`、`trigged[0..1]` 是同一组 4 bytes。播放时把这些实时 bool 状态解释为一个 `int`，可产生远超 MIDI 音域的数值。
- 该数值随后直接进入 `note1` 和 `mtof()`。超高频率会被限制到 16 kHz，同时对应 mode gain 被切到 0 并经过平滑，符合用户听到极短脉冲而非正常音符的现象。

本轮只定位，没有提交产品修复。

## MIDI 真机测试

设备：`/dev/cu.usbserial-11310`，CoreMIDI destination：`USB MIDI DevicePort 1`。测试生成成对 Note On/Note Off，pitch 按 36 到 95 循环，velocity 为 100。串口从 reset 到 snapshot 始终保持同一个连接，压力期间不逐消息打印。

| Source | Channel | Mode | Pairs requested | Interval us | Sent on/off | Wall time | Parsed | Callback on/off | Errors | RX high water | Result |
|---|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| CoreMIDI variable pitch | 1 | 未记录 | 100,000，故障后停止 | 1,000 | 未保留 host 终值 | 未保留 | 36,053 | 18,027 / 18,026 | 0 | 120 | 左 wet 消失；右 wet 和 dry/thru 正常 |
| Python fixed pitch 60 | 1 | 测试中被改为 String | 计划长测，人工停止 | 未记录 | 60,256 / 60,255 | 158.484 s | 120,511 | 60,256 / 60,255 | 0 | 120 | wet 仍正常，但测试中操作过两侧 Mode/面板音符键，不能作为单变量对照 |
| CoreMIDI variable pitch | 1 | String | 10,000 | 1,000 | 10,000 / 10,000 | 31.641 s | 20,000 | 10,000 / 10,000 | 0 | 120 | 左 wet 消失；右 wet 正常 |
| CoreMIDI variable pitch | 2 | String | 10,000 | 1,000 | 10,000 / 10,000 | 26.931 s | 20,000 | 10,000 / 10,000 | 0 | 120 | 右 wet 消失；该侧测试前右 wet 正常 |

### 故障后 marker

- 左侧 String 测试后发送 channel 1 pitch 55：`parsed` 从 20,000 增至 20,002，callback 从 10,000/10,000 增至 10,001/10,001，`left note0` 从 75 变为 55；左 wet 仍无声。
- 同一时刻发送 channel 2 pitch 67：callback 和 `right note0` 正常更新，右 wet 仍有声。
- 右侧 String 测试后发送 channel 2 pitch 57：`parsed` 从 20,000 增至 20,002，callback 从 10,000/10,000 增至 10,001/10,001，`right note0` 从 75 变为 57；右 wet 仍无声。
- 第一次左侧故障后曾用 CC 11 强制左侧接近全 wet，再发送 pitch 57。mix 和 pitch 参数均更新，左 wet 仍无声，因此不是 accidental mix=0。

### 无效的串口测试

最初使用每次重新打开串口的 `print` 命令。第一次长压力后出现 `POWERON_RESET`，运行状态和计数被重置，故该轮不参与边界判定。之后所有有效结果均使用持续串口 session。

## 边界判定

选择设计文档中的这一项：

> 回调和 DSP 参数都更新，但目标侧听感停止、对照侧正常：问题位于该侧 DSP/audio 状态。

证据：两次 10,000-pair 测试均满足 `parsed == on + off == 20,000`、`errors == 0`，故障后的 marker 继续更新 callback 和 Faust 参数；左右测试分别只令被测侧 wet 消失。dry/thru 始终存在，所以 codec/output 整体没有停止。

在产品层级，快速、不连续地改变 `pm.modeFilter` 频率系数而保留同一组高 Q 递归状态，是已确认的故障触发路径。`modeFilter` 的静态系数虽然稳定，但当前实现没有为快速换频提供状态转换、重置或交叉渐变。

## 离线对照与剩余边界

从原生成代码提取的 `mydsp` 在 macOS 上以 44.1 kHz、32-sample block、float、noise/sine 输入运行。Poly 和 String 两种模式、decay 5/10、32 到 160 samples 的 pitch 更新间隔均运行到 100,000 events，没有出现非有限输出或恒定 DC；String matrix 的 peak 为 0.453742。

因此离线结果不能证明真机内部已经出现 NaN/Inf，也不能排除 ESP32 编译、实时并发或浮点行为参与。若继续缩小数值根因，下一项单变量测试应在 ESP32 audio task 内记录每侧 9 个 `modeFilter` 递归状态的 finite/peak/constant 标志，并保持 MIDI、Mode 和输入刺激不变。

## Flash 安全边界

用户确认不需要完整 4 MiB flash 作为恢复锚点。诊断期间只写 app0 `0x10000`；bootloader、partition table、otadata 和 NVS 未写。恢复锚点为刷写前读取的当前 app0：

- `/private/tmp/wingie2-before-midi-debug-app0.bin`
- size: `0x140000`
- SHA-256: `6da9aca6795cc6ef9352705089c5c8aef58a91dc51d37c73592052ef2fa8c36b`

partition table 的只读 SHA-256 为 `8edc409c4d09a6ac4cb2b3029ad9aaf2edcd2c5e0e89137861b1018a7d21ee15`。
