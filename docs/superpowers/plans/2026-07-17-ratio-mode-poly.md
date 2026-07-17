# Ratio 模式复音化改造计划

目标：把 RATIO_MODE 从单音模式改为与 POLY_MODE 一样的 3 音复音模式（用户已拍板的设计决策见下）。

## 用户已确认的设计决策

- **Q1 = 方案 B**：声部 v（0/1/2）使用 `ratio_profile.ratios[3v..3v+2]`，即共鸣器 i 与 ratios[i] 1:1 对应。网页配置页新增"复制槽 1 的 ratio 到槽 2、3"按钮（纯前端实现）。
- **Q2**：保持 Ratio 原有八度偏移：设备按键音符 = `i + BASE_NOTE + oct[ch]*12 + (ch ? 12 : 0)`（不采用 Poly 的 +12/+24）。
- **Q3**：MPE/弯音完全向 Poly 看齐：MPE 逐声部分配（allocateVoice/releaseVoice），每声部独立弯音，弃用 mpeMonoState 单音路径。
- **Q4**：Tap Sequencer 仅 STRING/BAR 参与录制与回放；RATIO 与 POLY/CAVE 一样不参与（现状：POLY 本就不参与）。
- **Q5**：开机时 RATIO 通道的 3 个声部初始化为大三和弦 {0,4,7} + BASE_NOTE + oct[ch]*12 + (ch ? 12 : 0)。
- **Q6**：旧单音 Ratio 行为完全替换；Ratio 模式旋转 LED 动画保留；模式持久化、串口配置协议（仍 9 个 ratio、无新操作码）不变。

## 技术方案（固件，不重新编译 FAUST）

RATIO 仍走 DSP 的 cave 通路（`set_channel_dsp_mode` 不变：POLY→0，其余→1）。9 个共鸣器分 3 组，
声部 v 拥有共鸣器 {3v, 3v+1, 3v+2}，固件直接写 `cave_freq_i` 实现复音。

### 核心新函数（MPE.ino，与 poly 系列并列）

- `apply_ratio_voice_pitch(ch, voice)`：
  - `state = mpe_state.voices[ch][voice]`
  - `fundamental = configured_note_frequency(state.note) * wingie_mpe::pitchRatio(poly_total_bend(ch, voice))`
  - 对 k∈{0,1,2}：`i = 3*voice + k`，`freq = clamp(fundamental * ratio_profile.ratios[i], 16, 16000)`，`cm_freq_set(ch, i, freq)`
- 声部 (重新)分配 / 改音时 unmute 该声部组的 3 个共鸣器（`cm_mute_set(ch, i, false)`）。
- 把现有 `set_poly_voice_note` / `cycle_poly_voice_note` / `apply_poly_voice_pitch` / `apply_all_poly_voice_pitch`
  泛化为按 `Mode[ch]` 分发的共享函数（POLY→写 poly_note/poly_pitch_ratio；RATIO→写 cave_freq 组），
  或保留旧函数并新增 ratio 变体 + 统一分发入口。调用点：MPE.ino、MIDI.ino、control.ino。

### 分支调整清单

1. `MPE.ino refresh_mono_pitch`：移除 RATIO（仅 STRING/BAR 走 apply_pitched_mode_channel）。
2. `MPE.ino refresh_side_pitch`：`POLY_MODE || RATIO_MODE` 走全声部重应用。
3. `MPE.ino handle_mpe_note_on`：RATIO 从 mpeMonoState 分支移到 POLY 分支（allocateVoice + 应用声部音高）。
4. `MPE.ino handle_mpe_note_off`：RATIO 加入 releaseVoice 分支。
5. `MIDI.ino MIDISetPitch`：RATIO 从 mono 分支移到 poly 分支（记录 conventionalPitchChannel/Bend + 轮转声部）。
6. `Wingie2.ino set_channel_pitch`：移除 RATIO（仅 STRING/BAR）。
7. `Wingie2.ino apply_current_mode_parameters`：RATIO 分支改为 unmute 全部 + 全声部重应用。
8. `Wingie2.ino apply_note_profiles_to_dsp`：RATIO 改为全声部重应用（a3_freq / alt tuning 变化时）。
9. `serial_config.ino apply_ratio_profile_to_dsp`：RATIO 通道全声部重应用（替代 apply_pitched_mode_channel）。
10. `control.ino` 按键处理：mono 分支条件 `!= POLY && != CAVE` 收紧为仅 STRING/BAR（tap 录制随之排除 RATIO）；
    650 行附近 POLY 轮转分支扩为 `POLY || RATIO` 并按模式分发，RATIO 音符偏移保持 `ch ? 12 : 0`。
11. `control.ino` Tap Sequencer 回放块：仅 STRING/BAR 执行（RATIO 跳过 set_channel_note 与 mode_changed 触发）。
12. `control.ino` 开机初始化（337-342 行附近）：现有 POLY 三和弦初始化保持不变；
    其后对 `Mode[ch] == RATIO_MODE` 的通道追加 RATIO 三和弦初始化（{0,4,7} + BASE_NOTE + oct*12 + (ch?12:0)），
    后写生效。模式切换沿用当前声部音符（与 POLY 行为一致，不重新初始化）。
13. 检查 `midi_diagnostics.ino`、`stuff.ino` 中所有 RATIO/POLY 相关引用，保证语义一致。
14. LED（ratio_led_phase 旋转动画）、模式保存（dirty[8+ch]）、串口协议、MIDI CC 模式切换（127=RATIO）均不变。

## 网页配置页（Tools/wingie_config.html）

- ratio 表 9 行按 3 行一组视觉分组为槽 1/2/3（Slot 1/2/3），表头/帮助文本同步更新（中英 i18n，
  沿用现有 data-i18n-zh/en 模式），说明"Ratio 复音模式下声部 1/2/3 分别使用槽 1/2/3 的比例"。
- 新增按钮"复制槽 1 到槽 2、3 / Copy Slot 1 to Slots 2&3"：把 draft 中 ratios[0..2] 复制到
  ratios[3..5] 与 ratios[6..8]，标记未保存修改，并复用现有 150ms 防抖提交通路。纯前端，无协议变更。
- 同步更新 `tests/tools/test_wingie_config_html.py`。

## 测试

- `tests/dsp/ratio_mode_reference.py`：保留现有 mono 函数供 STRING/BAR 使用；新增
  `ratio_poly_voice_frequencies(fundamental, ratios, voice)` 返回声部组的 3 个频率（含 clamp）。
- `tests/dsp/ratio_mode_reference_test.py`：按新语义更新（3 声部分组、clamp、非法输入校验）。
- host C++ 测试（tests/host/*.cpp）所测头文件（mpe_state.h / config_profiles.h / serial_config_protocol.h /
  tap_sequence.h）本次不改动，预期全绿；用 g++ 逐个编译运行回归。

## 文档

- `README.md`：Ratio 模式描述改为 3 音复音（每声部 3 个分音、槽位分组、网页复制按钮、
  按键轮转、MPE 逐声部弯音、不再参与 Tap Sequencer）。
- `MPE.md`：涉及 RATIO 单音（mpeMonoState）的描述更新为逐声部。
- `ALT_TUNING.md`：如涉及 Ratio 单音行为则同步。

## 验证

- `arduino-cli compile --fqbn esp32:esp32:esp32 --libraries Libraries Wingie2`（仓库根目录执行）必须通过。
- `python -m pytest tests/dsp tests/tools -x -q` 全绿。
- host C++ 测试逐个 `g++` 编译运行通过。
- 硬件实测由用户后续进行（AGENTS.md 要求编译与真机验证分开报告）。
