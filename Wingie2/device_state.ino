#include "device_state.h"

// 全局自旋锁定义。所有跨任务共享状态访问见 device_state.h 头部说明。
portMUX_TYPE g_deviceMux = portMUX_INITIALIZER_UNLOCKED;

void mark_cave_changed(byte ch, byte bank) {
  taskENTER_CRITICAL(&g_deviceMux);
  cave_config_revision[ch][bank]++;
  cave_config_dirty[ch][bank] = true;
  taskEXIT_CRITICAL(&g_deviceMux);
}

void snapshot_cave_bank(byte ch, byte bank, float *freq_out, bool *mute_out,
                        uint32_t *revision_out, bool *dirty_out) {
  taskENTER_CRITICAL(&g_deviceMux);
  for (byte index = 0; index < wingie_config::kRatioCount; index++) {
    freq_out[index] = cm_freq[ch][bank][index];
    mute_out[index] = cm_ms[ch][bank][index];
  }
  *revision_out = cave_config_revision[ch][bank];
  *dirty_out = cave_config_dirty[ch][bank];
  taskEXIT_CRITICAL(&g_deviceMux);
}

void snapshot_cave_revision(byte ch, byte bank, uint32_t *revision_out) {
  taskENTER_CRITICAL(&g_deviceMux);
  *revision_out = cave_config_revision[ch][bank];
  taskEXIT_CRITICAL(&g_deviceMux);
}

void snapshot_cave_revisions(uint32_t revision_out[2][wingie_config::kCaveBankCount]) {
  taskENTER_CRITICAL(&g_deviceMux);
  for (byte ch = 0; ch < 2; ch++) {
    for (byte bank = 0; bank < wingie_config::kCaveBankCount; bank++) {
      revision_out[ch][bank] = cave_config_revision[ch][bank];
    }
  }
  taskEXIT_CRITICAL(&g_deviceMux);
}

uint32_t set_cave_bank_from_midi(byte ch, byte bank, byte voice, float freq) {
  taskENTER_CRITICAL(&g_deviceMux);
  cm_freq[ch][bank][voice] = freq;
  cave_config_revision[ch][bank]++;
  cave_config_dirty[ch][bank] = true;
  const uint32_t revision = cave_config_revision[ch][bank];
  taskEXIT_CRITICAL(&g_deviceMux);
  return revision;
}

bool set_cave_bank_atomic(byte ch, byte bank, const float *freqs, const bool *mutes,
                          bool has_expected, uint32_t expected_revision,
                          uint32_t *revision_out) {
  taskENTER_CRITICAL(&g_deviceMux);
  const bool conflict = has_expected && cave_config_revision[ch][bank] != expected_revision;
  if (!conflict) {
    for (byte index = 0; index < wingie_config::kRatioCount; index++) {
      cm_freq[ch][bank][index] = freqs[index];
      cm_ms[ch][bank][index] = mutes[index];
    }
    cave_config_revision[ch][bank]++;
    cave_config_dirty[ch][bank] = true;
  }
  *revision_out = cave_config_revision[ch][bank];
  taskEXIT_CRITICAL(&g_deviceMux);
  return !conflict;
}

bool consume_mode_change_request(byte ch) {
  taskENTER_CRITICAL(&g_deviceMux);
  const bool viaKeys = modeChangingFromKeys[ch];
  const bool viaMidi = modeChangingFromMIDI[ch];
  modeChangingFromKeys[ch] = false;
  modeChangingFromMIDI[ch] = false;
  if (viaKeys) {
    if (Mode[ch] < MODE_NUM) Mode[ch] += 1;
    else Mode[ch] = 0;
  }
  taskEXIT_CRITICAL(&g_deviceMux);
  return viaKeys || viaMidi;
}

void set_mode_from_midi(byte ch, int mode) {
  taskENTER_CRITICAL(&g_deviceMux);
  if (mode <= RATIO_MODE && Mode[ch] != mode) {
    Mode[ch] = mode;
    modeChangingFromMIDI[ch] = true;
  }
  taskEXIT_CRITICAL(&g_deviceMux);
}
