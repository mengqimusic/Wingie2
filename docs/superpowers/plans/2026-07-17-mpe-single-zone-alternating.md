# MPE 单 Zone 逐音交替 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 Wingie2 的 MPE 从"默认关闭的双 Zone（1–4 / 13–16）"重构为"常开的单 Zone（Manager Ch1 + 6 Member Ch2–7）+ 逐音左右交替分配"，并移除 `mpe_enabled` 开关使 MPE 成为系统唯一 MIDI 状态。

**Architecture:** MPE 状态机（`mpe_state.h`）保持通用双 Zone 结构但策略层只用 Lower Zone；Note On 时由固件按 `mpeFlip` 在左右引擎间交替分配（Cave 侧跳过），ownership 记录 `(channel,note)→(side,voice)`；Manager Ch1 为全局语义（PB 作用两侧全部 MPE voice，CC 两侧同效）；Upper MCM（Ch16 RPN 6）消费但忽略；Ch8–16 保留常规 Left/Right/Both 路由，出厂常规路由默认改为 8/9/10。

**Tech Stack:** ESP32 Arduino Core `2.0.4-cn`、Arduino MIDI Library、C++ host 测试（g++）、Python pytest（源码模式断言）、agent-browser + Chromium（网页 mock 测试）、CoreMIDI/Swift（真机激励）、pyserial（真机快照）。

## Global Constraints

- 编译：`arduino-cli compile --fqbn esp32:esp32:esp32 --libraries Libraries Wingie2`（Core 必须 `esp32:esp32 2.0.4-cn`，2.0.5 与 3.x 不兼容）。
- 不改 `Wingie2.dsp` 与生成的 `Wingie2/Wingie2.cpp`；本计划无 DSP 变更。
- 提交格式：`type(scope): 中文 msg`，message 逐文件说明改动，结尾 `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`；每个 Task 完成门禁后独立提交。
- 烧写只写 app0 `0x10000` + `verify_flash`，不读写备份、不写 NVS（AGENTS.md 既定例行程序）；esptool 3.3.0-cn 用下划线子命令（`write_flash`）。
- 真机 MIDI 只用 CoreMIDI destination `USB MIDI DevicePort 1`，绝不触碰 Port 2。
- 串口 `/dev/cu.usbserial-11310` @115200：**open/close 会复位 ESP32**，真机测试必须用单一常驻进程持有串口；boot 期间不读 MIDI。
- 真机验证中不得发送 `save` op（运行时 `set_param` 即可），测试结束刷回普通构建。
- 已存 NVS 偏好不做固件迁移（用户已同意）：旧设备保存的 `midi_ch_l/r/both=1/2/3` 落在 Zone 内将失效，由文档/release note 说明，固件不擅自改写。

---

### Task 1: mpe_state.h 单 Zone 启动布局 + host 测试

**Files:**
- Modify: `Wingie2/mpe_state.h`（常量区 line 10-17、删除 `configureDefaultDualZones` line 161-170）
- Test: `tests/host/mpe_state_test.cpp`（全文重写）

**Interfaces:**
- Consumes: 无（首个任务）。
- Produces: `wingie_mpe::kStartupMemberCount`（`static const uint8_t = 6`）——Task 2 的 `configure_mpe_startup()` 使用；`configureZone(kLowerZone, n)` 保持通用（Task 2 的 MCM 路径使用）；`configureDefaultDualZones` 被删除，后续任务不得引用。

- [ ] **Step 1: 重写 host 测试（先写失败测试）**

将 `tests/host/mpe_state_test.cpp` 全文替换为：

```cpp
#include <assert.h>
#include <math.h>

#include "../../Wingie2/mpe_state.h"

using namespace wingie_mpe;

void testStartupZoneClaimsSevenChannels() {
  State state;
  state.reset();
  assert(state.claimedChannels() == 0);
  state.configureZone(kLowerZone, kStartupMemberCount);
  assert(state.claimedChannels() == 0x007F);
  for (uint8_t channel = 1; channel <= 7; channel++) {
    assert(state.zoneForChannel(channel) == kLowerZone);
  }
  assert(state.zoneForChannel(8) == kNoZone);
  assert(state.zoneForChannel(16) == kNoZone);
  assert(state.channelIsManager(1));
  assert(!state.channelIsManager(2));
  assert(state.pitchBendRange(1).semitones == 2);
  assert(state.pitchBendRange(2).semitones == 48);
}

void testRecentZoneConfigurationWins() {
  State state;
  state.reset();
  assert(state.configureZone(kLowerZone, 7) == 0x00FF);
  const uint16_t changed = state.configureZone(kUpperZone, 11);
  assert(changed & channelBit(5));
  assert(changed & channelBit(16));
  assert(!(changed & channelBit(2)));
  assert(state.zoneForChannel(2) == kLowerZone);
  assert(state.zoneForChannel(4) == kLowerZone);
  assert(state.zoneForChannel(5) == kUpperZone);
  assert(state.zoneForChannel(15) == kUpperZone);
  state.configureZone(kLowerZone, 15);
  assert(!state.zoneIsActive(kUpperZone));
  assert(state.zoneForChannel(16) == kLowerZone);
}

void testPitchBendRangesAndEndpoints() {
  State state;
  state.reset();
  state.configureZone(kLowerZone, kStartupMemberCount);
  state.setPitchBend(2, kPitchBendMaximum);
  assert(fabsf(state.channelPitchBendSemitones(2) - 48.0f) < 0.0001f);
  state.setPitchBend(2, kPitchBendMinimum);
  assert(fabsf(state.channelPitchBendSemitones(2) + 48.0f) < 0.0001f);
  state.setPitchBendRange(3, 12, 50);
  assert(fabsf(rangeSemitones(state.pitchBendRange(2)) - 12.5f) < 0.0001f);
  state.setPitchBendRange(1, 7, 0);
  assert(fabsf(rangeSemitones(state.pitchBendRange(1)) - 7.0f) < 0.0001f);
  assert(fabsf(pitchRatio(12.0f) - 2.0f) < 0.0001f);
}

void testVoiceOwnershipAndStealing() {
  State state;
  state.reset();
  state.configureZone(kLowerZone, kStartupMemberCount);
  state.setPitchBend(2, 4096);
  assert(state.allocateVoice(0, 2, 60) == 0);
  assert(fabsf(state.voices[0][0].memberBendSemitones - 24.0f) < 0.01f);
  assert(state.allocateVoice(1, 3, 62) == 0);
  assert(fabsf(state.voices[1][0].memberBendSemitones - 0.0f) < 0.0001f);
  assert(state.allocateVoice(0, 3, 64) == 1);
  assert(state.allocateVoice(0, 4, 65) == 2);
  assert(state.allocateVoice(0, 2, 67) == 0);
  assert(state.releaseVoice(0, 2, 60) == -1);
  assert(state.releaseVoice(0, 2, 67) == 0);
  assert(!state.voices[0][0].active);
  const float releasedBend = state.voices[0][0].memberBendSemitones;
  state.setPitchBend(2, 0);
  assert(state.voices[0][0].memberBendSemitones == releasedBend);
  state.setPitchBend(1, kPitchBendMaximum);
  assert(fabsf(state.managerPitchBendSemitones(kLowerZone) - 2.0f) < 0.0001f);
}

void testConventionalAndMpePitchRemainIsolated() {
  assert(fabsf(totalPitchBend(false, 2.0f, -1.0f, 6.0f) - 2.0f) < 0.0001f);
  assert(fabsf(totalPitchBend(true, 2.0f, -1.0f, 6.0f) - 5.0f) < 0.0001f);
}

int main() {
  testStartupZoneClaimsSevenChannels();
  testRecentZoneConfigurationWins();
  testPitchBendRangesAndEndpoints();
  testVoiceOwnershipAndStealing();
  testConventionalAndMpePitchRemainIsolated();
  return 0;
}
```

注意 `testRecentZoneConfigurationWins` 保留：状态机的通用 `configureZone`（含 Upper 分支）仍然保留并被测试，只是固件策略层不再调用 Upper（见 Task 2）。

- [ ] **Step 2: 编译运行，确认失败**

Run: `g++ -std=c++11 -Wall tests/host/mpe_state_test.cpp -o /tmp/mpe_state_test && /tmp/mpe_state_test`
Expected: 编译错误 `error: 'kStartupMemberCount' is not a member of 'wingie_mpe'`

- [ ] **Step 3: 修改 mpe_state.h**

在 `Wingie2/mpe_state.h` 常量区（`static const int8_t kNoZone = -1;` 之后）加一行：

```cpp
static const uint8_t kStartupMemberCount = 6;
```

删除 `configureDefaultDualZones`（原 line 161-170，死代码：唯一调用方 `configure_mpe_power_on` 将在 Task 2 移除）：

```cpp
  uint16_t configureDefaultDualZones(bool enabled) {
    const uint16_t before = claimedChannels();
    zones[kLowerZone].memberMask = 0;
    zones[kUpperZone].memberMask = 0;
    if (enabled) {
      configureZone(kLowerZone, kVoiceCount);
      configureZone(kUpperZone, kVoiceCount);
    }
    return before ^ claimedChannels();
  }
```

- [ ] **Step 4: 编译运行，确认通过；回归其余 host 测试**

Run:
```bash
g++ -std=c++11 -Wall tests/host/mpe_state_test.cpp -o /tmp/mpe_state_test && /tmp/mpe_state_test && echo MPE_PASS
g++ -std=c++11 -Wall tests/host/config_profiles_test.cpp -o /tmp/config_profiles_test && /tmp/config_profiles_test && echo PROFILES_PASS
g++ -std=c++11 -Wall tests/host/serial_config_protocol_test.cpp -o /tmp/serial_config_protocol_test && /tmp/serial_config_protocol_test && echo PROTOCOL_PASS
g++ -std=c++11 -Wall tests/host/tap_sequence_test.cpp -o /tmp/tap_sequence_test && /tmp/tap_sequence_test && echo TAP_PASS
```
Expected: 四个 PASS。

- [ ] **Step 5: Commit**

```bash
git add Wingie2/mpe_state.h tests/host/mpe_state_test.cpp
git commit -m "$(cat <<'EOF'
refactor(mpe): 状态机改为单 Zone 启动布局

- Wingie2/mpe_state.h：新增 kStartupMemberCount=6；删除已无调用方的 configureDefaultDualZones（configure_mpe_power_on 将在后续任务移除）。

- tests/host/mpe_state_test.cpp：以 Ch1–7 单 Zone 布局替换默认双 Zone 断言；voice ownership 测试覆盖跨侧分配；保留通用 configureZone 重叠优先级测试。

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 2: 固件单 Zone 交替 + 常开 + 移除 mpe_enabled

**Files:**
- Modify: `Wingie2/MPE.ino`（重写分配/刷新/配置路径）
- Modify: `Wingie2/Wingie2.ino:143,147`（全局声明）
- Modify: `Wingie2/control.ino:89-91,104,133-135`（出厂默认与启动配置）
- Modify: `Wingie2/stuff.ino:99-106`（删除 dirty[10] 保存块）
- Modify: `Wingie2/serial_config.ino:353-363`（删除 set_param 的 mpe_enabled 分支；get_settings 与 schema 在 Task 3）
- Modify: `Wingie2/midi_diagnostics.ino:89-96`（快照输出）

**Interfaces:**
- Consumes: `wingie_mpe::kStartupMemberCount`、`configureZone(kLowerZone, n)`（Task 1）。
- Produces: `configure_mpe_startup()`（control.ino 启动调用）；`mpeFlip`（全局交替状态，`Wingie2.ino` 声明）；`mpe_manager_bend()` 无参全局版本；`configure_mpe_zone(byte memberCount)` 单参数版本（MCM 路径）。`MIDI.ino` 不需要改动（conventional 路由判定不变）。

- [ ] **Step 1: Wingie2.ino 全局声明**

`Wingie2/Wingie2.ino` line 143：

```cpp
bool realtime_value_valid[3] = {true, true, true}, polyFlip = false, mpeFlip = false;
```

line 147 改为（删除 `mpe_enabled`）：

```cpp
bool unq_caves_store = false;
```

- [ ] **Step 2: MPE.ino 重写**

将 `Wingie2/MPE.ino` 中下列函数整体替换（未列出的函数 `set_poly_voice_dsp`、`apply_poly_voice_pitch`、`apply_all_poly_voice_pitch`、`set_poly_voice_note`、`cycle_poly_voice_note`、`clear_mpe_mono_assignment`、`reset_mpe_assignments`、`refresh_mono_pitch`、`refresh_side_pitch`、`reset_mpe_performance`、`set_conventional_side_pitch`、`set_conventional_channel_note` 保持不变）：

`mpe_manager_bend` / `mono_total_bend` / `poly_total_bend`（Manager 改为全局单 Zone）：

```cpp
float mpe_manager_bend() {
  return mpe_state.managerPitchBendSemitones(wingie_mpe::kLowerZone);
}

float mono_total_bend(byte ch) {
  return wingie_mpe::totalPitchBend(mpeMonoState[ch].channel != 0, conventionalPitchBend[ch],
                                    mpe_manager_bend(), mpeMonoState[ch].memberBendSemitones);
}

float poly_total_bend(byte ch, byte voice) {
  const wingie_mpe::VoiceState &state = mpe_state.voices[ch][voice];
  return wingie_mpe::totalPitchBend(state.channel != 0, conventionalPitchBend[ch],
                                    mpe_manager_bend(), state.memberBendSemitones);
}
```

`initialize_mpe_state`（增加 flip 复位）：

```cpp
void initialize_mpe_state() {
  mpe_state.reset();
  memset(mpeMonoState, 0, sizeof(mpeMonoState));
  mpeFlip = false;
}
```

`configure_mpe_zone` / `configure_mpe_startup`（替换原双参数 `configure_mpe_zone` 与 `configure_mpe_power_on`，后者整体删除）：

```cpp
void configure_mpe_zone(byte memberCount) {
  reset_mpe_performance(mpe_state.configureZone(wingie_mpe::kLowerZone, memberCount));
  if (serial_config_ready) refresh_mpe_zone_pitch();
}

void configure_mpe_startup() {
  configure_mpe_zone(wingie_mpe::kStartupMemberCount);
}
```

新增 `mpe_note_side` 与重写 `handle_mpe_note_on` / `handle_mpe_note_off`：

```cpp
// 单 Zone 逐音交替：Cave 侧不参与分配，音符全部落到可发声侧；两侧均 Cave 时吞掉。
int8_t mpe_note_side() {
  const bool leftPlayable = Mode[0] != CAVE_MODE;
  const bool rightPlayable = Mode[1] != CAVE_MODE;
  if (!leftPlayable && !rightPlayable) return -1;
  if (!leftPlayable) return 1;
  if (!rightPlayable) return 0;
  const byte side = mpeFlip ? 1 : 0;
  mpeFlip = !mpeFlip;
  return side;
}

bool handle_mpe_note_on(byte channel, byte pitch) {
  if (mpe_state.zoneForChannel(channel) != wingie_mpe::kLowerZone) return false;
  const int8_t side = mpe_note_side();
  if (side < 0) return true;
  const byte ch = static_cast<byte>(side);
  if (Mode[ch] == POLY_MODE) {
    const int voice = mpe_state.allocateVoice(ch, channel, pitch);
    if (voice >= 0) apply_poly_voice_pitch(ch, voice);
  } else if (Mode[ch] == STRING_MODE || Mode[ch] == BAR_MODE || Mode[ch] == RATIO_MODE) {
    mpeMonoState[ch].active = true;
    mpeMonoState[ch].channel = channel;
    mpeMonoState[ch].note = pitch;
    mpeMonoState[ch].memberBendSemitones = mpe_state.memberPitchBendSemitones(channel);
    set_channel_pitch(ch, pitch, mono_total_bend(ch));
  }
  return true;
}

bool handle_mpe_note_off(byte channel, byte pitch) {
  if (mpe_state.zoneForChannel(channel) != wingie_mpe::kLowerZone) return false;
  // ownership 记录 (channel,note)->(side,voice)，Note Off 需在两侧查找归属。
  for (byte ch = 0; ch < 2; ch++) {
    if (Mode[ch] == POLY_MODE) {
      if (mpe_state.releaseVoice(ch, channel, pitch) >= 0) return true;
    } else if (mpeMonoState[ch].active && mpeMonoState[ch].channel == channel && mpeMonoState[ch].note == pitch) {
      mpeMonoState[ch].active = false;
      return true;
    }
  }
  return true;
}
```

`refresh_mpe_zone_pitch` 改为无参（单 Zone，跨两侧刷新），新增 `refresh_mpe_member_pitch`：

```cpp
void refresh_mpe_zone_pitch() {
  for (byte ch = 0; ch < 2; ch++) {
    for (byte voice = 0; voice < wingie_mpe::kVoiceCount; voice++) {
      wingie_mpe::VoiceState &state = mpe_state.voices[ch][voice];
      if (state.active && !mpe_state.channelIsManager(state.channel)) {
        state.memberBendSemitones = mpe_state.memberPitchBendSemitones(state.channel);
      }
    }
    if (mpeMonoState[ch].active && !mpe_state.channelIsManager(mpeMonoState[ch].channel)) {
      mpeMonoState[ch].memberBendSemitones = mpe_state.memberPitchBendSemitones(mpeMonoState[ch].channel);
    }
    refresh_side_pitch(ch);
  }
}

void refresh_mpe_member_pitch(byte channel) {
  for (byte ch = 0; ch < 2; ch++) {
    for (byte voice = 0; voice < wingie_mpe::kVoiceCount; voice++) {
      wingie_mpe::VoiceState &state = mpe_state.voices[ch][voice];
      if (state.active && state.channel == channel) {
        state.memberBendSemitones = mpe_state.memberPitchBendSemitones(channel);
      }
    }
    if (mpeMonoState[ch].active && mpeMonoState[ch].channel == channel) {
      mpeMonoState[ch].memberBendSemitones = mpe_state.memberPitchBendSemitones(channel);
    }
    refresh_side_pitch(ch);
  }
}
```

`apply_pitch_bend_range`（Zone 判定改单 Zone）：

```cpp
void apply_pitch_bend_range(byte channel, byte controller, byte value) {
  wingie_mpe::PitchBendRange range = mpe_state.pitchBendRange(channel);
  if (controller == 6) range.semitones = value;
  if (controller == 38) range.cents = value;
  mpe_state.setPitchBendRange(channel, range.semitones, range.cents);
  if (mpe_state.zoneForChannel(channel) == wingie_mpe::kLowerZone) {
    refresh_mpe_zone_pitch();
    return;
  }
  for (byte ch = 0; ch < 2; ch++) {
    if (conventionalPitchChannel[ch] == channel) set_conventional_side_pitch(ch, channel);
  }
}
```

`handle_mpe_rpn` 中 MCM 分支（只有 Ch1 生效；Ch16 Upper MCM 消费但忽略）：

```cpp
  if (mpe_state.selectedRpnIs(channel, 0, 6)) {
    // 单 Zone 策略：仅 Ch1 的 MCM 生效；Ch16 Upper MCM 被消费但忽略（见 MPE.md）。
    if (number == 6 && channel == 1) configure_mpe_zone(value);
    return true;
  }
```

`handle_mpe_control_change`（Manager CC 全局两侧）：

```cpp
bool handle_mpe_control_change(byte channel, byte number, byte value) {
  if (handle_mpe_rpn(channel, number, value)) return true;
  if (mpe_state.zoneForChannel(channel) != wingie_mpe::kLowerZone) return false;
  if (mpe_state.channelIsManager(channel)) {
    // 单 Manager 全局语义：Ch1 CC 同时作用左右两侧。
    MIDISetParam(0, number, value);
    MIDISetParam(1, number, value);
  }
  return true;
}
```

`handlePitchBend`（Manager PB 刷两侧，Member PB 按通道跨侧刷新）：

```cpp
void handlePitchBend(byte channel, int bend) {
#if MIDI_DIAGNOSTICS
  recordMidiPitchBend(channel, bend);
#endif
  mpe_state.setPitchBend(channel, bend);
  if (mpe_state.zoneForChannel(channel) == wingie_mpe::kLowerZone) {
    if (mpe_state.channelIsManager(channel)) {
      refresh_side_pitch(0);
      refresh_side_pitch(1);
    } else {
      refresh_mpe_member_pitch(channel);
    }
    return;
  }
  if (channel == midi_ch_l) set_conventional_side_pitch(0, channel);
  if (channel == midi_ch_r) set_conventional_side_pitch(1, channel);
  if (channel == midi_ch_both) {
    set_conventional_side_pitch(0, channel);
    set_conventional_side_pitch(1, channel);
  }
}
```

- [ ] **Step 3: control.ino 出厂默认与启动**

line 89-91（NVS 首次初始化块内）：

```cpp
    prefs.putUChar("midi_ch_l", 8);
    prefs.putUChar("midi_ch_r", 9);
    prefs.putUChar("midi_ch_both", 10);
```

删除该块内的 `prefs.putBool("mpe_enabled", false);`（line 104）。

line 133-135 替换：

```cpp
  configure_mpe_startup();
  Serial.printf("mpe single zone claimed = 0x%04x\n", mpe_state.claimedChannels());
```

（删除 `mpe_enabled = prefs.getBool("mpe_enabled", false);`、`configure_mpe_power_on(mpe_enabled);`、`Serial.printf("mpe_enabled = %d\n", mpe_enabled);`。）

- [ ] **Step 4: stuff.ino 删除 mpe_enabled 保存块**

删除 line 99-106：

```cpp
  if (dirty[10]) {
    dirty[10] = false;
    if (store.putBool("mpe_enabled", mpe_enabled)) Serial.printf("mpe_enabled is saved, value is %d.\n", mpe_enabled);
    else {
      dirty[10] = true;
      saved = false;
    }
  }
```

`dirty[11]` 数组保留（其余索引语义不变，index 10 不再使用）。

- [ ] **Step 5: midi_diagnostics.ino 快照更新**

`printMidiDiagnostics` 中 MPE 段（line 89-96）替换为：

```cpp
  Serial.printf("MPE claimed=0x%04x flip=%d manager_pb=%.3f pb_count=%lu last_pb_ch=%u last_pb=%d\n",
                mpe_state.claimedChannels(), mpeFlip, mpe_manager_bend(),
                (unsigned long)midiDiagnostic.pitchBendCount,
                midiDiagnostic.lastPitchBendChannel, midiDiagnostic.lastPitchBend);
  for (byte ch = 0; ch < 2; ch++) {
    Serial.printf("MPE side=%u mono_ch=%u mono_active=%d mono_member_pb=%.3f\n",
                  ch, mpeMonoState[ch].channel,
                  mpeMonoState[ch].active, mpeMonoState[ch].memberBendSemitones);
```

（原逐侧 `manager_pb=%.3f` 打印改为单行全局；voice 循环打印保持不变。）

- [ ] **Step 6: serial_config.ino 删除 mpe_enabled set_param 分支**

`applyScalarParameter` 中删除（line 353-363）——它是 `configure_mpe_power_on` 的唯一剩余调用方，不删则本任务编译失败；get_settings 字段与 schema 4 在 Task 3 处理：

```cpp
  if (strcmp(request.name, "mpe_enabled") == 0) {
    int enabled = 0;
    if (!quantizeIntegerParameter(request.value, 0, 1, enabled)) return false;
    if (mpe_enabled != static_cast<bool>(enabled)) {
      mpe_enabled = enabled;
      configure_mpe_power_on(mpe_enabled);
      dirty[10] = true;
    }
    canonical = mpe_enabled ? 1.0f : 0.0f;
    return true;
  }
```

- [ ] **Step 7: 编译普通与诊断构建**

Run:
```bash
rm -rf /tmp/wingie2-normal-build /tmp/wingie2-midi-diag-build
arduino-cli compile --fqbn esp32:esp32:esp32 --libraries Libraries --output-dir /tmp/wingie2-normal-build Wingie2
arduino-cli compile --fqbn esp32:esp32:esp32 --libraries Libraries \
  --build-property compiler.cpp.extra_flags=-DMIDI_DIAGNOSTICS=1 \
  --output-dir /tmp/wingie2-midi-diag-build Wingie2
```
Expected: 两个构建均成功；无 `mpe_enabled` / `configure_mpe_power_on` 未定义错误。

- [ ] **Step 8: Commit**

```bash
git add Wingie2/MPE.ino Wingie2/Wingie2.ino Wingie2/control.ino Wingie2/stuff.ino Wingie2/midi_diagnostics.ino Wingie2/serial_config.ino
git commit -m "$(cat <<'EOF'
feat(mpe): 单 Zone 逐音左右交替并常开

- Wingie2/MPE.ino：Note On 经 mpe_note_side 在左右引擎间交替分配（Cave 侧跳过，两侧均 Cave 吞掉）；Note Off 跨两侧查找 ownership；Manager Ch1 全局语义（PB 刷两侧、CC 两侧同效）；MCM 仅 Ch1 生效，Ch16 Upper MCM 消费但忽略；refresh_mpe_zone_pitch 改无参跨侧版，新增 refresh_mpe_member_pitch；删除 configure_mpe_power_on。

- Wingie2/Wingie2.ino：删除全局 mpe_enabled，新增 mpeFlip 交替状态。

- Wingie2/control.ino：启动即建单 Zone（6 Member，Ch1–7）；出厂 midi_ch_l/r/both 默认 8/9/10；删除 mpe_enabled 偏好读写。

- Wingie2/stuff.ino：删除 dirty[10] 的 mpe_enabled 保存块。

- Wingie2/serial_config.ino：删除 set_param 的 mpe_enabled 分支（configure_mpe_power_on 的最后调用方）；get_settings 字段与 schema 升级在后续任务处理。

- Wingie2/midi_diagnostics.ino：快照输出 claimed/flip/全局 manager_pb，移除 startup_enabled。

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: 串口协议移除 mpe_enabled + schema 4

**Files:**
- Modify: `Wingie2/serial_config.ino:385-388,406`（353-363 的 set_param 分支已在 Task 2 删除）
- Test: `tests/tools/test_wingie_config_html.py:257-258`（固件源码断言部分）

**Interfaces:**
- Consumes: 无固件新接口（`configure_mpe_power_on` 已在 Task 2 删除，此处删除其串口调用方）。
- Produces: get_settings 响应不再含 `mpe_enabled`；hello `config_schema` 为 `4`（Task 4 网页端据此适配）。

- [ ] **Step 1: 先改 python 测试（失败测试）**

`tests/tools/test_wingie_config_html.py` line 257-258 替换：

```python
        self.assertNotIn("mpe_enabled", self.storage_source)
        self.assertNotIn("mpe_enabled", self.firmware_source)
        self.assertIn('"capabilities\\\":[\\\"settings\\\",\\\"ratio_mode\\\",\\\"cave_config\\\",\\\"mpe\\\"]', self.firmware_source)
        self.assertIn('"config_schema\\\":4', self.firmware_source)
```

- [ ] **Step 2: 运行确认失败**

Run: `python3 -m pytest tests/tools/test_wingie_config_html.py -x -q`
Expected: FAIL。Task 2 已删除 stuff.ino 保存块与 set_param 分支，`assertNotIn("mpe_enabled", storage_source)` 应已通过；失败点应为 get_settings 仍输出 `mpe_enabled` 字段且 `config_schema\":4` 断言不满足（firmware_source 为 serial_config.ino）。

- [ ] **Step 3: serial_config.ino 修改**

`sendSettings` 响应格式（line 385-388）替换：

```cpp
  response.append(",\"shared\":{\"a3_hz\":%.2f,\"tuning\":%d,"
                  "\"pre_clip_gain\":%.4f,\"post_clip_gain\":%.4f,"
                  "\"midi\":{\"left\":%d,\"right\":%d,\"both\":%d}}}",
                  a3_freq, use_alt_tuning ? alt_tuning_index : -1,
                  pre_clip_gain, post_clip_gain, midi_ch_l, midi_ch_r, midi_ch_both);
```

`sendHello`（line 406）`"config_schema\":3` 改为 `"config_schema\":4`（capabilities 保留 `"mpe"`——MPE 仍是设备能力，只是不再可开关）。

- [ ] **Step 4: 运行确认通过 + 编译回归**

Run:
```bash
python3 -m pytest tests/tools/test_wingie_config_html.py -x -q
arduino-cli compile --fqbn esp32:esp32:esp32 --libraries Libraries --output-dir /tmp/wingie2-normal-build Wingie2
```
Expected: pytest PASS；编译成功（无 `mpe_enabled` 未定义）。

- [ ] **Step 5: Commit**

```bash
git add Wingie2/serial_config.ino tests/tools/test_wingie_config_html.py
git commit -m "$(cat <<'EOF'
feat(serial): 移除 mpe_enabled 字段，配置 schema 升至 4

- Wingie2/serial_config.ino：删除 set_param 的 mpe_enabled 分支与 get_settings 响应字段；hello 的 config_schema 3->4（capabilities 保留 mpe）。

- tests/tools/test_wingie_config_html.py：存储源码断言改为不含 mpe_enabled；新增 config_schema 4 断言。

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 4: 网页配置移除 MPE 开关 + 适配 schema 4

**Files:**
- Modify: `Tools/wingie_config.html:545,609,843`
- Modify: `tests/tools/wingie_serial_mock.js:29,81,103,125`
- Test: `tests/tools/test_wingie_config_html.py:143-153`、`tests/tools/wingie_config_browser_test.sh:5,38,274,282,306,320,324`

**Interfaces:**
- Consumes: Task 3 的 schema 4 与无 `mpe_enabled` 的 get_settings。
- Produces: 网页无 MPE 字段；浏览器 mock 测试通过。

- [ ] **Step 1: 改 python 测试的网页字段断言（失败测试）**

`tests/tools/test_wingie_config_html.py` shared 字段列表（line 143-153）中删除 `"mpe_enabled",`，并在该循环后加：

```python
        self.assertNotIn("param:shared:mpe_enabled", self.source)
        self.assertNotIn("wg-mpe-enabled", self.source)
        self.assertIn("config_schema) !== 4", self.source)
```

- [ ] **Step 2: 运行确认失败**

Run: `python3 -m pytest tests/tools/test_wingie_config_html.py -x -q`
Expected: FAIL（html 仍含 `wg-mpe-enabled` / schema 3 检查）。

- [ ] **Step 3: wingie_config.html 修改**

删除 line 545 整个字段 div：

```html
          <div class="wg-field wg-field-full"><label for="wg-mpe-enabled" data-i18n-zh="开机 MPE 双 Zone" data-i18n-en="MPE Dual Zones at Startup"></label><select id="wg-mpe-enabled" class="wg-select" data-value-key="param:shared:mpe_enabled" disabled><option value="0" data-i18n-zh="关闭" data-i18n-en="Off"></option><option value="1" data-i18n-zh="开启：Lower→左，Upper→右" data-i18n-en="On: Lower→Left, Upper→Right"></option></select></div>
```

删除 line 609 描述符条目（含前导逗号与换行）：

```javascript
,
        "param:shared:mpe_enabled": {target: "shared", name: "mpe_enabled", path: ["shared", "mpe_enabled"], min: 0, max: 1, step: 1, immediate: true}
```

line 843 schema 检查：

```javascript
          if (hello.device !== "Wingie2" || Number(hello.config_schema) !== 4) throw new Error(languageText("需要 Wingie2 配置 schema 4 固件。", "Wingie2 config schema 4 firmware is required."));
```

- [ ] **Step 4: wingie_serial_mock.js 修改**

- settings.shared（line 29）：删除 `mpe_enabled: false` 及其前导换行/逗号（`midi: {left: 13, right: 14, both: 15}` 后不再有任何字段）。
- parameterSpec shared（line 81）：删除 `,\n        mpe_enabled: [0, 1, 1, true]`（`midi_both` 行为 shared 最后一项）。
- setParameter（line 103）：删除整个分支 `else if (request.target === "shared" && request.name === "mpe_enabled") settings.shared.mpe_enabled = Boolean(value);`（保留 midi_ 前缀分支与 else 主分支）。
- hello（line 125）：`config_schema: 3` 改为 `config_schema: 4`，capabilities 保留 `"mpe"`。

- [ ] **Step 5: wingie_config_browser_test.sh 修改**

- line 5：`SESSION="wingie-config-schema4-$$"`。
- line 38：`"schema 4 connection"`。
- line 274：删除 `exported.settings.shared.mpe_enabled = true;`，在其位置加 `exported.settings.shared.midi.both = 9;`。
- line 282 替换为：

```bash
  assert(imported.settings.shared.midi.both === 9, `import omitted shared midi_both: ${JSON.stringify(mock.writes.filter((request) => request.op === "set_param"))}`);
```

- line 306：sharedRows 数组删除 `"#wg-mpe-enabled"`。
- line 320：删除整行 `if (top("#wg-midi-left") === top("#wg-mpe-enabled")) throw new Error("MPE and MIDI channel fields share the same row");`。
- line 324：`printf 'Wingie2 schema 4 configuration browser mock tests passed.\n'`。

- [ ] **Step 6: 运行 python 与浏览器测试**

Run:
```bash
python3 -m pytest tests/tools/test_wingie_config_html.py -q
./tests/tools/wingie_config_browser_test.sh
```
Expected: pytest PASS；浏览器脚本输出 `Wingie2 schema 4 configuration browser mock tests passed.`

- [ ] **Step 7: Commit**

```bash
git add Tools/wingie_config.html tests/tools/wingie_serial_mock.js tests/tools/test_wingie_config_html.py tests/tools/wingie_config_browser_test.sh
git commit -m "$(cat <<'EOF'
feat(web): 移除 MPE 开关字段并适配 schema 4

- Tools/wingie_config.html：删除“开机 MPE 双 Zone”字段与其 param 描述符；握手要求 config_schema 4。

- tests/tools/wingie_serial_mock.js：mock 设置、参数规格、setParam 分支移除 mpe_enabled；hello 报告 schema 4。

- tests/tools/test_wingie_config_html.py：shared 字段列表移除 mpe_enabled，新增字段缺失与 schema 4 断言。

- tests/tools/wingie_config_browser_test.sh：导入/导出断言改用 midi_both；布局断言移除 MPE 字段；会话与文案更新为 schema 4。

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 5: 文档（MPE.md 重写 + README/ALT_TUNING 更新 + 迁移说明）

**Files:**
- Modify: `MPE.md`（全文重写）
- Modify: `README.md:27,54,63-89`（MPE 章节与 Save 清单）
- Modify: `ALT_TUNING.md:20-26`（MPE 调律段）

**Interfaces:**
- Consumes: Task 1–4 的全部行为决策。
- Produces: 与固件行为一致的协议文档；从双 Zone 固件迁移的说明。

- [ ] **Step 1: 重写 MPE.md**

全文替换为：

```markdown
# Wingie2 MPE

Wingie2 implements the MIDI Polyphonic Expression 1.1 member-channel note, note-ownership, and
Pitch Bend path over its existing MIDI 1.0 input, as a single always-on Lower Zone with per-note
left/right alternating engine assignment. This scope does not map Channel Pressure or CC 74 to
synthesis parameters.

## Zone and Routing

MPE is always on: there is no enable switch. At startup Wingie2 claims one Lower Zone — Manager
Channel 1 with 6 Member Channels 2–7. RPN 6 MPE Configuration Messages on Channel 1 may resize or
disable the Zone at runtime; a restart restores the startup layout. MCM received on Channel 16
(Upper Zone) is consumed but ignored, so a dual-zone controller must be reconfigured to a single
zone or its upper-zone notes on Channels 13–15 will not sound.

Channels 8–16 keep the conventional Left/Right/Both routing (factory defaults: Left 8, Right 9,
Both 10). Channel 16 global-settings CC and Channel 14/15 Cave-frequency CC remain reachable.

## Note Assignment and Pitch Bend

- Each Note On in the Zone is assigned to one engine side, alternating left/right in arrival
  order (the same free-running alternation as the conventional Both route). A side in Cave Mode
  is skipped: all notes land on the sounding side; if both sides are in Cave Mode, notes are
  ignored.
- Poly Mode binds each Note On to one of three voices on its assigned side. A free voice is used
  first; when all three are active, the oldest voice is replaced.
- Member Pitch Bend changes every active voice owned by that Member Channel, on either side.
- Manager Pitch Bend (Channel 1) is global: it changes every active MPE voice on both sides.
- Manager CC (Channel 1) is applied to both sides; Member CC is not mapped.
- String, Bar, and Ratio use the latest Note On as a monophonic owner on the assigned side. Member
  control stops after its matching Note Off, while the last pitch remains latched and Manager
  Pitch Bend remains active.
- Note Off is routed by (channel, note) ownership across both sides.

Member Pitch Bend defaults to ±48 semitones and Manager Pitch Bend defaults to ±2 semitones. RPN 0
is accepted on Manager and Member Channels; a Member range received on one channel applies to
every Member. Pitch Bend state is tracked before Note On so an MPE source can establish a note's
initial microtonal offset.

## Tuning

Pitch Bend is applied after the base note has been resolved through Wingie2's current A3 and
tuning. An MPE source can therefore provide alternate tuning by sending per-note Pitch Bend before
Note On. Select Standard internal tuning when the source should be the only tuning authority. If
an internal alternate tuning remains enabled, the internal interval and MPE offset are both
applied.

## Migrating from the dual-zone firmware

- MPE no longer has an on/off preference; the USB configuration page's MPE field is removed and
  `get_settings` no longer reports `mpe_enabled` (config schema 4).
- The Zone occupies Channels 1–7, so conventional routes saved as Channels 1/2/3 no longer apply.
  Set Left/Right/Both to Channels 8+ in the USB configuration page (factory defaults for new
  devices are 8/9/10; existing saved settings are not rewritten by the firmware).
- A conventional (non-MPE) controller should use Channel 1 or Channels 8–16. On Channels 2–7 its
  Pitch Bend is interpreted as MPE Member Pitch Bend (default ±48 semitones) and its CC is not
  mapped.
```

- [ ] **Step 2: README.md 更新**

- line 27（中文 Save 清单）：删除 `、开机 MPE 双 Zone`（句变为 `…三个 MIDI 通道路由、左右 Mode 与 Input…`）。
- line 54（英文 Save 清单）：删除 `the MPE dual-zone startup setting, `。
- line 63-89 两个 MPE 章节替换为（中英各一段）：

```markdown
## MPE

MPE 常开，无开关：Lower Zone 固定 Manager Channel 1 + Member Channels 2–7，每个 Note On 按到达
顺序在左右引擎间交替分配（Cave 侧跳过）；Manager Pitch Bend/CC 为全局（作用两侧），Member
Pitch Bend 逐音；Channel 8–16 保留常规 Left/Right/Both 路由（出厂默认 8/9/10）。RPN 6 可在
运行时调整或撤销 Zone，Channel 16 的 Upper MCM 被忽略；RPN 0 支持 Manager/Member 弯音量程
（默认 ±2 / ±48）。MPE 来源可在 Note On 前发送逐音 Pitch Bend 实现外部 alternate tuning，
详见 [`MPE.md`](MPE.md) 与 [`ALT_TUNING.md`](ALT_TUNING.md)。当前实现范围是 Zone、逐音
ownership 与 Pitch Bend，不映射 Channel Pressure 或 CC 74。

MPE is always on: a single Lower Zone (Manager Channel 1, Member Channels 2–7) assigns each
Note On to the left/right engines in alternating arrival order (Cave-mode sides are skipped).
Manager Pitch Bend/CC is global (both sides); Member Pitch Bend is per-note. Channels 8–16 keep
the conventional Left/Right/Both routing (factory defaults 8/9/10). RPN 6 may resize or disable
the Zone at runtime; Upper-Zone MCM on Channel 16 is ignored. RPN 0 sets Manager/Member bend
ranges (±2/±48 default). An MPE source can provide alternate tuning via per-note Pitch Bend
before Note On — see [`MPE.md`](MPE.md) and [`ALT_TUNING.md`](ALT_TUNING.md). Channel Pressure
and CC 74 are not mapped.
```

- [ ] **Step 3: ALT_TUNING.md 更新**

line 20-26 段落末尾（`Select Standard in the USB configuration page…` 所在句后）追加一句：

```markdown
The MPE zone layout (Manager Channel 1, Member Channels 2–7, alternating sides) is documented in
[`MPE.md`](MPE.md).
```

- [ ] **Step 4: 提交前核对文档与固件行为一致**

Run: `grep -n "mpe_enabled\|双 Zone\|Dual Zone" MPE.md README.md ALT_TUNING.md`
Expected: 无输出（除 MPE.md `## Migrating` 一节对旧行为的引用——该节允许出现 `mpe_enabled` 字样；若有输出确认仅限该节）。

- [ ] **Step 5: Commit**

```bash
git add MPE.md README.md ALT_TUNING.md
git commit -m "$(cat <<'EOF'
docs(mpe): 单 Zone 交替协议文档与迁移说明

- MPE.md：全文重写为常开单 Zone（Ch1–7）+ 逐音左右交替 + 全局 Manager 语义；记录 Upper MCM 忽略策略、常规通道范围与双 Zone 固件迁移说明。

- README.md：MPE 章节中英重写；Save 持久化清单移除开机 MPE 双 Zone。

- ALT_TUNING.md：MPE 调律段补充 Zone 布局指引。

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 6: 全门禁 + 真机验证 + 结果文档

**Files:**
- Create: `docs/superpowers/results/2026-07-17-mpe-single-zone-results.md`
- Modify: 无固件文件（本任务为验证）

**Interfaces:**
- Consumes: Task 1–5 全部产出。
- Produces: 双构建产物、真机验证证据、结果文档；设备最终刷回普通构建。

- [ ] **Step 1: 全部自动门禁**

Run:
```bash
g++ -std=c++11 -Wall tests/host/mpe_state_test.cpp -o /tmp/mpe_state_test && /tmp/mpe_state_test && echo MPE_PASS
g++ -std=c++11 -Wall tests/host/config_profiles_test.cpp -o /tmp/config_profiles_test && /tmp/config_profiles_test && echo PROFILES_PASS
g++ -std=c++11 -Wall tests/host/serial_config_protocol_test.cpp -o /tmp/serial_config_protocol_test && /tmp/serial_config_protocol_test && echo PROTOCOL_PASS
g++ -std=c++11 -Wall tests/host/tap_sequence_test.cpp -o /tmp/tap_sequence_test && /tmp/tap_sequence_test && echo TAP_PASS
python3 -m pytest tests/tools tests/dsp -q
./tests/tools/wingie_config_browser_test.sh
rm -rf /tmp/wingie2-normal-build /tmp/wingie2-midi-diag-build
arduino-cli compile --fqbn esp32:esp32:esp32 --libraries Libraries --output-dir /tmp/wingie2-normal-build Wingie2
arduino-cli compile --fqbn esp32:esp32:esp32 --libraries Libraries \
  --build-property compiler.cpp.extra_flags=-DMIDI_DIAGNOSTICS=1 \
  --output-dir /tmp/wingie2-midi-diag-build Wingie2
```
Expected: 4 个 host PASS；pytest 全绿；浏览器 `schema 4 ... passed`；两个固件构建成功。记录两个 bin 的 SHA-256（`shasum -a 256`）。

- [ ] **Step 2: 烧写诊断构建**

Run:
```bash
ESPTOOL=$(find /Users/mengwu/Library/Arduino15/packages/esp32/tools/esptool_py -name esptool -type f | head -1)
"$ESPTOOL" --chip esp32 --port /dev/cu.usbserial-11310 write_flash 0x10000 /tmp/wingie2-midi-diag-build/Wingie2.ino.bin
"$ESPTOOL" --chip esp32 --port /dev/cu.usbserial-11310 verify_flash 0x10000 /tmp/wingie2-midi-diag-build/Wingie2.ino.bin
```
Expected: write + `verify OK (digest matched)`。

- [ ] **Step 3: 启动常驻串口驱动**

串口 open 会复位设备，必须用单一常驻进程。编译激励工具并启动驱动：

```bash
swiftc -O -o /tmp/midi_stress Tools/midi_stress.swift
```

将以下驱动写入 `/tmp/mpe_single_zone_driver.py` 并后台运行（写日志 `/tmp/wingie_log.txt`，从 `/tmp/wingie_next_cmd` 读单行命令：`p` 快照 / `midi <args>` 调 /tmp/midi_stress / `set <target> <name> <value>` 发 set_param 帧 / `get` get_settings / `quit`）：

```python
#!/usr/bin/env python3
import json, os, subprocess, time
import serial

CMD_FILE = "/tmp/wingie_next_cmd"
LOG = open("/tmp/wingie_log.txt", "a", buffering=1)

def log(text=""):
    LOG.write(str(text) + "\n")

def collect(ser, duration=2.0, quiet=0.5):
    out = bytearray()
    deadline = time.monotonic() + duration
    quiet_deadline = time.monotonic() + quiet
    while time.monotonic() < deadline and time.monotonic() < quiet_deadline:
        chunk = ser.read(4096)
        if chunk:
            out.extend(chunk)
            quiet_deadline = time.monotonic() + quiet
    return out.decode("utf-8", errors="replace")

def main():
    if os.path.exists(CMD_FILE):
        os.unlink(CMD_FILE)
    ser = serial.Serial()
    ser.port = "/dev/cu.usbserial-11310"
    ser.baudrate = 115200
    ser.timeout = 0.05
    ser.dtr = False
    ser.rts = False
    ser.open()
    log(collect(ser, duration=10.0, quiet=2.0))
    log("=== READY ===")
    while True:
        if os.path.exists(CMD_FILE):
            with open(CMD_FILE) as f:
                cmd = f.read().strip()
            os.unlink(CMD_FILE)
            log(f">>> {cmd}")
            parts = cmd.split()
            try:
                if cmd == "p":
                    ser.reset_input_buffer()
                    ser.write(b"p")
                    log(collect(ser))
                elif cmd == "get":
                    ser.reset_input_buffer()
                    ser.write(b'@{"v":1,"id":1,"op":"get_settings"}\n')
                    log(collect(ser))
                elif parts[0] == "set":
                    ser.reset_input_buffer()
                    frame = {"v": 1, "id": 1, "op": "set_param", "target": parts[1],
                             "name": parts[2], "value": float(parts[3])}
                    ser.write(("@" + json.dumps(frame, separators=(",", ":")) + "\n").encode())
                    log(collect(ser))
                elif parts[0] == "midi":
                    result = subprocess.run(["/tmp/midi_stress"] + parts[1:],
                                            capture_output=True, text=True, timeout=90)
                    log(result.stdout.strip() or result.stderr.strip())
                    time.sleep(0.3)
                elif cmd == "quit":
                    break
            except Exception as exc:
                log(f"ERROR {cmd!r}: {exc}")
            log(f"<<< DONE {cmd}")
        time.sleep(0.05)

main()
```

发送命令的方式：`echo 'p' > /tmp/wingie_next_cmd && sleep 3 && tail -30 /tmp/wingie_log.txt`。

- [ ] **Step 4: 真机验证序列**

依次执行（每步后 `p` 快照，期望值如下；PB 换算：正值 b/8191×range，负值 b/8192×range）：

1. 基线：`p` → `claimed=0x007f flip=0`，errors=0。
2. `midi note-on 2 60 0` → 左侧 `voice=0 note=60 ch=2 active=1`，`flip=1`（交替到左）。
3. `midi note-on 2 64 0` → **右侧** `voice=0 note=64 ch=2 active=1`，`flip=0`（交替到右）。
4. `midi note-on 3 67 2048` → 左侧 `voice=1 note=67 ch=3 member_pb=12.001`，`flip=1`。
5. `midi bend 1 4096` → `manager_pb=1.000`；左侧 voice1 `total_pb=13.001`、右侧 voice0 `total_pb=1.000`（全局 Manager 两侧同效）。
6. `midi note-off 2 60` → 左侧 voice0 `active=0`（跨侧 ownership 释放）。
7. `midi mpe-config 0 0` → `claimed=0x0000`；`midi mpe-config 6 0` → `claimed=0x007f`（Ch16 Upper MCM 已发送但被忽略——若 claimed 出现 0x8000 段则失败）。
8. `set shared midi_left 8` → ok；`midi note-on 8 50 0`、`midi bend 8 4096` → 常规路径：左侧某 voice `ch=0` 且 `total_pb=1.000`，MPE voice（ch=2/3）total 不含此 +1.000（隔离）。
9. Manager CC 全局：swift 无裸 CC 子命令，用 Ch1 `pb-range` 之外的替代——跳过 CC 数值验证，改由 get_settings 验证路由：`get` → 响应不含 `mpe_enabled`、含 `"midi":{"left":8,...}`。
10. `set shared mpe_enabled 1` → 响应 `ok:false`（字段已移除）。
11. `set left mode 3`（Cave）→ `midi note-on 2 70 0` → 音符落到**右侧**（跳过 Cave 侧），左侧无新 voice；`set left mode 0` 恢复。
12. `midi mpe-config 0 0` 收尾；`quit` 停止驱动。

- [ ] **Step 5: 刷回普通构建并最终确认**

Run:
```bash
ESPTOOL=$(find /Users/mengwu/Library/Arduino15/packages/esp32/tools/esptool_py -name esptool -type f | head -1)
"$ESPTOOL" --chip esp32 --port /dev/cu.usbserial-11310 write_flash 0x10000 /tmp/wingie2-normal-build/Wingie2.ino.bin
"$ESPTOOL" --chip esp32 --port /dev/cu.usbserial-11310 verify_flash 0x10000 /tmp/wingie2-normal-build/Wingie2.ino.bin
sleep 5
python3 - <<'EOF'
import serial, time
c = serial.Serial(); c.port = "/dev/cu.usbserial-11310"; c.baudrate = 115200
c.timeout = 0.05; c.dtr = False; c.rts = False; c.open()
time.sleep(3)
c.reset_input_buffer()
c.write(b'@{"v":1,"id":1,"op":"get_settings"}\n')
out = bytearray(); deadline = time.monotonic() + 3
while time.monotonic() < deadline:
    chunk = c.read(4096)
    if chunk: out.extend(chunk); deadline = time.monotonic() + 0.8
print(out.decode("utf-8", errors="replace"))
c.close()
EOF
```
Expected: verify OK；get_settings 响应无 `mpe_enabled` 字段。

- [ ] **Step 6: 写结果文档并提交**

创建 `docs/superpowers/results/2026-07-17-mpe-single-zone-results.md`，包含：实现边界、自动验证结果（host/pytest/浏览器/双构建大小与 SHA-256）、真机逐步期望值与实测值、剩余边界（未听音、未评价手感；旧设备 midi_ch 1/2/3 需手动改 8+），然后：

```bash
git add docs/superpowers/results/2026-07-17-mpe-single-zone-results.md
git commit -m "$(cat <<'EOF'
test(mpe): 真机验证单 Zone 逐音交替

- docs/superpowers/results/2026-07-17-mpe-single-zone-results.md：记录双构建、host/web 门禁与真机验证结果（交替分配、全局 Manager、Upper MCM 忽略、Cave 跳过、常规通道隔离、schema 4 字段移除）。

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
EOF
)"
```

---

## Self-Review 记录

- **Spec coverage：** 单 Zone 6 Member（Task 1/2）、逐音交替 + Cave 跳过（Task 2 `mpe_note_side`）、移除开关常开（Task 2/3/4）、Manager 全局（Task 2）、忽略 Upper MCM（Task 2 + Task 5 文档）、交替自由轮转无复位（Task 2 `initialize_mpe_state` 仅 boot 复位）、出厂 8/9/10 + 不迁移（Task 2 + Global Constraints + Task 5 迁移说明）、串口/网页字段移除（Task 3/4）、真机验证（Task 6）。
- **Placeholder scan：** 各步骤均含完整代码/命令；Task 6 Step 4-9 CC 数值验证因子命令缺失改为 get_settings 验证，已注明。
- **Type consistency：** `configure_mpe_zone(byte)` 单参数版本在 Task 2 定义并被 `handle_mpe_rpn`/`configure_mpe_startup` 使用；`refresh_mpe_zone_pitch()` 无参版本在 Task 2 定义并被 `apply_pitch_bend_range`/`configure_mpe_zone` 使用；`kStartupMemberCount` Task 1 定义、Task 2 使用；诊断快照的 `mpe_manager_bend()` 无参与 Task 2 定义一致。
