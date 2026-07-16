# MPE Pitch Bend 实现与真机结果

## 实现边界

- MPE 1.1 Lower Zone 映射左侧，Upper Zone 映射右侧；开机双 Zone 默认关闭，可由 USB 配置保存；
- RPN 6 在运行时建立、调整或撤销 Zone，最近的重叠配置优先；
- Poly 每侧 3 voice 维护 `Member Channel + note -> voice` ownership，满载时替换最旧 voice；
- Member Pitch Bend 只作用于所属 voice，Manager Pitch Bend 作用于整侧；
- String、Bar、Ratio 使用最新音的单音 PB；Cave 忽略 Note/PB；
- RPN 0 支持 Manager/Member PB range，默认分别为 ±2 / ±48 semitone；
- 普通 MIDI 与 MPE 共存：MPE 已占通道优先，未占通道继续 Left/Right/Both 路由；
- 当前范围不映射 Channel Pressure 或 CC 74。

## 自动验证

- host C++：`mpe_state_test`、`config_profiles_test`、`serial_config_protocol_test`、`tap_sequence_test` 全部通过；
- DSP Python：22 tests 通过；Tools Python：53 tests 通过；
- `wingie_config_browser_test.sh` 通过真实 Chromium + Web Serial mock；
- `swiftc -typecheck Tools/midi_stress.swift` 通过；
- ESP32 Core `2.0.4-cn` 普通构建通过：flash `1205645 / 1310720` bytes（91%），RAM globals `51540 / 327680` bytes（15%）；
- 诊断构建通过：flash `1208129 / 1310720` bytes（92%），RAM globals `51596 / 327680` bytes（15%）。

## 真机验证

设备：ESP32-D0WDQ6 rev 1，USB Serial `/dev/cu.usbserial-11310`，CoreMIDI destination
`USB MIDI DevicePort 1`。未读取或备份 app0；上传只写标准 bootloader、partition、boot_app0 与 app
区域，NVS `0x9000` 未写入。

1. MCM `lower=3, upper=3` 后诊断 claimed mask 为 `0xf00f`；
2. 左 Poly：Ch 2 / 3 / 4 分别绑定 voice 0 / 1 / 2，PB 得到 `+24.003 / -12.000 / 0.000`
   semitone；
3. Ch 2 Note Off 后，再发 Ch 2 PB 不改变 released voice 的 Member PB；Ch 1 Manager PB `+2.000`
   仍作用于整侧；
4. 右 Poly：Ch 15 Member PB `+12.001` 与 Ch 16 Manager PB `-1.000` 合成为 `+11.001`
   semitone；
5. String：Note Off 后最后音高锁存，后续 Member PB 不再改变该音；
6. 未占用 Ch 5 继续按普通 MIDI 左路由工作；
7. RPN 0 将 Member range 改为 ±12 后，半量 PB 得到 `+6.001` semitone；
8. 测试发现普通 Ch 5 PB 曾错误叠加到 MPE voice；隔离 ownership 后复测为 `+6.001`，不再包含
   普通 PB；
9. USB `mpe_enabled=1/0` 即时得到 claimed mask `0xf00f/0x0000`；未 Save，重启后仍为 false；
10. 隔离修复后的普通构建曾刷回并由 `get_settings` 确认 MPE false、MIDI 1/2/3、Tuning Standard；
    最后一处 MCM 重配收窄随后完成编译、上传和 flash hash 校验，但设备在上传后同时退出 USB Serial
    与 CoreMIDI 枚举，因此无法补做这一最终二进制的重启后在线确认。

## 剩余边界

- 以上真机结果验证 MIDI parser、Zone/RPN、ownership、PB 数值、DSP 参数路径、配置和持久化边界；
- 设备实体 Volume 当时为 `0.0`，未进行扬声器/耳机实际听音，也未评价 PB 手感；需要使用真实 MPE
  控制器和音频输入完成最终演奏 smoke test。
- 仓库生成 C++ 来自 Faust 2.59.6，本机只有 2.85.5。`Wingie2.dsp` 已由 2.85.5 编译验证；为避免把
  生成架构从约 0.5 MB 无关升级到约 4.5 MB，本次只将新增 pitch-ratio 参数对应的 generated user
  section 同步到现有 2.59.6 C++，并通过 ESP32 构建与真机运行验证。
