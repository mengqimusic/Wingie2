#ifndef WINGIE2_DEVICE_STATE_H
#define WINGIE2_DEVICE_STATE_H

// 跨任务共享状态模块。
//
// 并发模型（评审 H1 已确认）：
//   - loop()（MIDI 回调 + serial config）与 control 任务都钉在 core 1、优先级 1，
//     由 FreeRTOS tick 抢占，任何跨任务读改写必须串行化；
//   - DSP 音频任务在 core 0 只读 dsp 参数，不触碰本模块状态；
//   - 唯一的真 ISR（Pressed0/Pressed1）只置 volatile bool 标志，不触碰本模块状态。
//
// ESP32 Arduino Core 2.0.4 的 noInterrupts()/interrupts() 是空宏，不能作为任务级
// 互斥手段；本模块统一用 g_deviceMux 自旋锁临界区（taskENTER_CRITICAL/EXIT_CRITICAL）。
// 约定：临界区内只做内存读改写，禁止调用任何可能阻塞的函数
// （I2C / Serial / NVS / dsp.setParamValue 等一律放在临界区外）。

#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config_profiles.h"

// 全局自旋锁：串行化 loop() 与 control 任务对本模块状态的访问。
// 定义于 device_state.ino。
extern portMUX_TYPE g_deviceMux;

// cave bank 配置事务：revision++ 与 dirty=true 必须在同一临界区内完成，
// 保证 serial config 的快照/条件写看到一致的 (revision, dirty) 对。
void mark_cave_changed(byte ch, byte bank);

// 通用设置 dirty 位索引（stuff.ino save_general_preferences 保存块与各写入点共用）
enum SettingsDirtyIndex {
  kDirtyMidiLeft = 0,
  kDirtyMidiRight,
  kDirtyMidiBoth,
  kDirtyA3Frequency,
  kDirtyLeftThreshold,
  kDirtyRightThreshold,
  kDirtyPreClipGain,
  kDirtyPostClipGain,
  kDirtyLeftMode,
  kDirtyRightMode,
  kDirtyMpeEnabled,
  kDirtyLineInputMono,
  kDirtyCount
};

// 一次性快照整个 bank：freq/mute/revision/dirty 在同一临界区内读出，
// 供 serial config 组装响应，替代无实效的 noInterrupts() 包夹。
void snapshot_cave_bank(byte ch, byte bank, float *freq_out, bool *mute_out,
                        uint32_t *revision_out, bool *dirty_out);

// 单一 bank 的 revision 快照（serial config set_cave 的冲突预检）。
void snapshot_cave_revision(byte ch, byte bank, uint32_t *revision_out);

// 全部 bank 的 revision 快照（serial config status）。
void snapshot_cave_revisions(uint32_t revision_out[2][wingie_config::kCaveBankCount]);

// MIDI cave CC：写单个 voice 频率 + mark_cave_changed，返回写后 revision。
uint32_t set_cave_bank_from_midi(byte ch, byte bank, byte voice, float freq);

// serial config set_cave 条件写事务：期望 revision 匹配时整 bank 写盘并置
// dirty，返回写后 revision；冲突时不修改任何状态并返回 false。
// 注意：DSP 应用（applyCaveBank 等可能阻塞的调用）必须由调用方放在临界区外。
bool set_cave_bank_atomic(byte ch, byte bank, const float *freqs, const bool *mutes,
                          bool has_expected, uint32_t expected_revision,
                          uint32_t *revision_out);

// 模式切换标志事务：原子读-清 modeChangingFromKeys / modeChangingFromMIDI；
// keys 触发时在本事务内递增 Mode（与 MIDI/网页对 Mode 的写串行化，避免丢更新）。
// 返回是否有待应用的模式切换。应用动作（apply_channel_mode_change）由调用方执行。
bool consume_mode_change_request(byte ch);

// MIDI CC_MODE：原子写 Mode 并挂起 modeChangingFromMIDI 标志。
void set_mode_from_midi(byte ch, int mode);

#endif  // WINGIE2_DEVICE_STATE_H
