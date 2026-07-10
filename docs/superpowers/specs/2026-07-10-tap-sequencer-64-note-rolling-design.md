# Wingie2 Tap Sequencer 64-Note Rolling 设计

## 目标

修复 Tap Sequencer 在第 13 个音开始越界读写的问题，并把产品行为明确为：每侧最多保存最近 64 个面板音符，超过容量时淘汰最早的音，始终按现存最早音到最新音循环。

本项只改变 Tap Sequencer 的存储边界，不改变 Poly/Cave mode、DSP 算法、MIDI 输入或 Preferences。

## 当前故障

当前状态由 `seq[2][12]`、`seqLen`、`writeHeadPos` 和 `playHeadPos` 四组松散数组组成。首音写入 index 0，后续音先递增 write head 和 length 再写入，但没有容量检查。第 13 个音写入 index 12，越过数组边界；播放端随后按照同样无上限的 length 读取。

正常构建的 ELF 显示右侧 `seq[1][12]` 与 `threshChanged`、`trigged` 的 bool bytes 重叠。越界数据被当作 `int` 音符写入 Faust，导致极高、极短的异常脉冲。

## 行为定义

- 每侧容量固定为 64 个音符。
- 音符保存面板 key index `0..11`，使用 `uint8_t`，不保存加过 octave/base-note 的 DSP pitch。
- 一次新的 first press 清空旧序列并以该音作为 index 0。
- 在容量未满时，后续音追加到尾部。
- 容量已满时，删除 index 0，把原 index `1..63` 左移到 `0..62`，并把新音写到 index 63。
- 播放顺序始终是当前数组的 index 0 到 `count - 1`，然后回到 index 0。
- 一个音只保持当前音高，不启动循环；两个及以上音才构成 Tap Sequence。这与当前 `seqLen != 0` 的行为一致。
- 左右两侧拥有完全独立的 sequence state。

## 状态模型

新增纯 C++ `TapSequence` 类型，拥有：

```cpp
static constexpr uint8_t kCapacity = 64;
uint8_t notes[kCapacity];
uint8_t count;
uint8_t playIndex;
```

它只提供以下操作：

- `bool reset(uint8_t note)`：`note < 12` 时设置 `count = 1`、`playIndex = 0` 和 `notes[0] = note` 并返回 true；非法 note 返回 false 且不改变状态。
- `bool append(uint8_t note)`：`note < 12` 时执行追加或 rolling 并返回 true；非法 note 返回 false 且不改变状态。
- `hasCycle()`：仅当 `count > 1` 时返回 true。
- `bool advance(uint8_t& note)`：仅在 `hasCycle()` 为 true 时把 play index 前进一格、写出合法音符并返回 true；否则返回 false 且不改变输出参数。

Rolling 时必须保持 playback cursor 的逻辑位置：

- `playIndex > 0` 时减 1，使其继续指向左移后的同一个音。
- `playIndex == 0` 时，原先指向的最早音已被删除，把它设为 63；下一次 `advance()` 将回到新的 index 0。

`control.ino` 不再直接修改 count、write head 或 play head。面板按键路径调用 `reset/append`，trigger 路径调用 `hasCycle/advance`，DSP pitch 的 base-note/octave 计算保持原样。

## 文件边界

- Create: `Wingie2/tap_sequence.h`，只包含固定容量状态与边界操作，不依赖 Arduino API。
- Modify: `Wingie2/Wingie2.ino`，用两个 `TapSequence` 实例替代四组全局数组。
- Modify: `Wingie2/control.ino`，接入 `reset/append/advance`。
- Create: `tests/host/tap_sequence_test.cpp`，使用标准 C++ assertions 验证状态机。
- Create after hardware test: `docs/superpowers/results/2026-07-10-tap-sequencer-64-note-rolling-results.md`，记录 host、build 和真机结果。

`tests/host/` 专门存放不依赖 Arduino、可由主机 C++ 编译器直接运行的固件逻辑测试；不放硬件模拟或生成文件。

## 验证

Host test 必须覆盖：

- reset 后只有一个音且 `hasCycle()` 为 false；
- 2、12、64 个音的追加和完整循环顺序；
- 第 65 个音淘汰第 1 个音并保留第 `2..65` 个音；
- 连续追加远超过 64 个音后，始终只保留最近 64 个；
- rolling 前后的 playback cursor 连续性；
- 左右两个实例互不影响；
- 所有可返回的音符均在 `0..11`；
- 非法 key index 不进入状态。

固件验证必须包括：

- ESP32 Arduino Core 2.0.4 默认构建和诊断构建；
- 真机在 String 和 Bar mode 重做原来的 `>12` 录入流程；
- 循环中没有超高短脉冲、没有跨侧污染，顺序与录入一致；
- 64-note rolling 的完整顺序由 host test 作为确定性证据，不依赖人工准确录入 65 音。

## 提交边界

TapSequence 测试和实现构成一个独立修复，不与 Faust DSP 修改混在同一提交。该修复可以在不改变生成 DSP 文件的情况下单独构建、烧录和回退。
