# Wingie2 MIDI 单通道停止响应诊断设计

## 目标

在真实 Wingie2 硬件上复现 Ableton Live 向单一 MIDI 通道连续发送大量 Note On/Note Off 后，该通道停止响应而另一通道仍正常的问题，并确定故障边界位于 UART/MIDI 解析、消息回调、参数写入还是 DSP。

本轮只增加临时诊断能力并收集证据，不修复产品行为。

## 约束

- 使用项目要求的 ESP32 Arduino Core 2.0.4 和 `ESP32 Dev Module` 目标。
- MIDI 输入使用 `USB MIDI DevicePort 1`，串口调试使用 `/dev/cu.usbserial-11310`，波特率 115200。
- 压力测试期间不得逐条打印 MIDI 消息，避免串口日志改变被测时序。
- 不改变 MIDI 通道、tuning、cave 或其他 Preferences。
- 刷写前读取并保存完整 flash；测试固件只写当前 app partition，不改 bootloader、partition table 或 NVS。
- 左右通道分别测试，不能用其中一侧的结果代替另一侧。

## 临时诊断固件

诊断状态由固定大小的全局计数器组成，不分配动态内存。对 MIDI channel 1 和 2 分别记录：

- Note On 回调数量；
- Note Off 回调数量；
- 最近一次 pitch 和 velocity；
- `currentPoly`、`Mode` 和对应 DSP 音高参数快照。

全局记录：

- `MIDI.read()` 调用数量和成功解析的消息数量；
- MIDI parser error callback 次数和最后错误位；
- `Serial2.available()` 的最高水位；
- 诊断计数开始后的毫秒数。

USB debug serial 提供两个单字符命令：

- `r`：原子地清零诊断计数；
- `p`：在压力批次结束后打印一次完整快照。

Note Off 回调只计数，不改变现有乐器行为。诊断命令只在用户明确发送字符时执行，不增加周期性日志或超时逻辑。

## 测试刺激

CoreMIDI 生成器模拟 Ableton 的成对事件，在单一 MIDI 通道上循环发送 pitch 36 到 95，velocity 固定为非零值：

1. 低速基线批次，确认发送数、解析数和回调数一致。
2. 逐步提高到 DIN MIDI 接近满线速。
3. 每个目标通道依次发送 1,000、10,000、100,000 个 Note On/Note Off 对。
4. 每批结束后先打印计数，再分别向目标侧和对照侧发送低速 marker note。
5. 若 CoreMIDI 生成器未复现，则用 Ableton Live 在同一 `USB MIDI DevicePort 1` 上播放等价 clip，并复用相同串口计数。

批次之间清零计数，且一次只改变通道、事件数或速率中的一个变量。
每发送一个 Note On/Note Off 对，预期 Note On 和 Note Off 计数各增加 1，成功解析消息数增加 2。

## 判定

- 发送数大于解析数：问题位于 CoreMIDI destination 之后、消息回调之前，范围包括 USB MIDI interface、DIN 链路、UART 缓冲和 MIDI parser；结合 RX 最高水位与 parser error 继续缩小范围。
- parser error 增长：至少有消息到达 parser，但输入字节流不再构成合法 MIDI 消息。
- 解析数完整但目标通道回调数不足：问题位于消息过滤或 callback dispatch。
- 回调计数和最后 pitch 正常，但 DSP 参数未更新：问题位于 `MIDISetPitch` 到 Faust UI 参数写入路径。
- 回调和 DSP 参数都更新，但目标侧听感停止、对照侧正常：问题位于该侧 DSP/audio 状态。
- 两侧同时停止：不符合当前用户报告，应作为不同故障单独记录。

## 安全与恢复

刷写前保存完整 flash 镜像、flash ID、partition table 和原 app partition。诊断固件通过完整 Arduino 编译后，使用 `esptool` 只写 app partition。测试结束后只写回原 app partition，并通过 Wingie2 启动日志、保存的 MIDI 通道及 tuning 状态确认恢复。完整 flash 写回仅作为 bootloader 或 partition table 意外改变时的兜底。仓库中的诊断改动不与后续产品修复混在同一提交。

## 验证输出

最终报告包括每批的发送数量、速率、解析数量、各通道回调数量、parser error、RX 最高水位、marker note 响应和串口重启/看门狗信息。听感结果与计数结果分别报告。
