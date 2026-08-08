// 通用设置保存项表：key（NVS 键名）、类型（0=UChar, 1=Float, 2=Bool,
// 3=Float 存 offset）、dirty 索引、成功后的 Serial 输出格式、取值地址（volatile 兼容）。
// 输出格式逐字保留历史格式，表序即保存/打印顺序。
struct GeneralSettingEntry {
  const char *key;
  uint8_t type;
  uint8_t dirtyIndex;
  const char *format;
  const volatile void *value;
};

enum GeneralSettingType {
  kSettingUChar = 0,
  kSettingFloat = 1,
  kSettingBool = 2,
  kSettingA3Offset = 3,
};

static const GeneralSettingEntry kGeneralSettings[kDirtyCount] = {
  {"midi_ch_l", kSettingUChar, kDirtyMidiLeft, "%s is saved, value is %d.\n", &midi_ch_l},
  {"midi_ch_r", kSettingUChar, kDirtyMidiRight, "%s is saved, value is %d.\n", &midi_ch_r},
  {"midi_ch_both", kSettingUChar, kDirtyMidiBoth, "%s is saved, value is %d.\n", &midi_ch_both},
  {"a3_freq_offset", kSettingA3Offset, kDirtyA3Frequency, "a3_freq_offset (%.2fHz) is saved. a3 = %.2fHz.\n", &a3_freq},
  {"left_thresh", kSettingFloat, kDirtyLeftThreshold, "%s is saved, value is %.4f\n", &left_thresh},
  {"right_thresh", kSettingFloat, kDirtyRightThreshold, "%s is saved, value is %.4f\n", &right_thresh},
  {"pre_clip_gain", kSettingFloat, kDirtyPreClipGain, "%s is saved, value is %.4f\n", &pre_clip_gain},
  {"post_clip_gain", kSettingFloat, kDirtyPostClipGain, "%s is saved, value is %.4f\n", &post_clip_gain},
  {"left_mode", kSettingUChar, kDirtyLeftMode, "%s is saved, value is %d.\n", &Mode[0]},
  {"right_mode", kSettingUChar, kDirtyRightMode, "%s is saved, value is %d.\n", &Mode[1]},
  {"mpe_enabled", kSettingBool, kDirtyMpeEnabled, "%s is saved, value is %d.\n", &mpe_enabled},
};

bool save_general_preferences(Preferences &store) {
  bool saved = true;
  for (const GeneralSettingEntry &entry : kGeneralSettings) {
    if (!dirty[entry.dirtyIndex]) continue;
    dirty[entry.dirtyIndex] = false;
    bool entrySaved = false;
    switch (entry.type) {
      case kSettingUChar: {
        const int value = *static_cast<const volatile int *>(entry.value);
        entrySaved = store.putUChar(entry.key, value);
        if (entrySaved) Serial.printf(entry.format, entry.key, value);
        break;
      }
      case kSettingFloat: {
        const float value = *static_cast<const volatile float *>(entry.value);
        entrySaved = store.putFloat(entry.key, value);
        if (entrySaved) Serial.printf(entry.format, entry.key, value);
        break;
      }
      case kSettingBool: {
        const bool value = *static_cast<const volatile bool *>(entry.value);
        entrySaved = store.putBool(entry.key, value);
        if (entrySaved) Serial.printf(entry.format, entry.key, value);
        break;
      }
      case kSettingA3Offset: {
        const float frequency = *static_cast<const volatile float *>(entry.value);
        const float freq_offset = frequency - wingie_config::kDefaultA3Frequency;
        entrySaved = store.putFloat(entry.key, freq_offset);
        if (entrySaved) Serial.printf(entry.format, freq_offset, frequency);
        break;
      }
    }
    if (!entrySaved) {
      dirty[entry.dirtyIndex] = true;
      saved = false;
    }
  }

  bool tuningSaveNeeded = tuning_preferences_dirty;
  if (unq_caves_store) {
    for (int ch = 0; ch < 2; ch++) {
      for (int bank = 0; bank < 3; bank++) {
        if (unquantized_cave_config_dirty[ch][bank] ||
            unquantized_cave_storage_migration_pending[ch][bank]) tuningSaveNeeded = true;
      }
    }
  }
  if (tuningSaveNeeded) {
    tuning_preferences_dirty = false;
    const int useAltTuning = use_alt_tuning;
    const int tuningIndex = alt_tuning_index;
    const bool unquantizedStored = unq_caves_store;
    bool tuningSaved = !unquantizedStored || save_unquantized_cave_preferences(store);
    if (!store.putUChar("use_alt_tuning", useAltTuning)) tuningSaved = false;
    else Serial.printf("use_alt_tuning is saved, value is %d.\n", useAltTuning);
    if (!store.putChar("alt_tuning_idx", tuningIndex)) tuningSaved = false;
    else Serial.printf("alt_tuning_index is saved, value is %d.\n", tuningIndex);
    if (!tuningSaved || !store.putBool("unq_caves_store", unquantizedStored)) {
      tuningSaved = false;
      Serial.println("Failed to save unquantized Cave backup metadata");
    } else {
      Serial.printf("unq_caves_store is saved, value is %d\n", unquantizedStored);
    }
    if (!tuningSaved) {
      tuning_preferences_dirty = true;
      saved = false;
    }
  }
  return saved;
}

bool save_all_preferences() {
  if (!prefs.begin("settings", RW_MODE)) return false;
  bool saved = true;
  if (unq_caves_store) {
    if (!save_general_preferences(prefs)) saved = false;
    if (!save_ratio_and_cave_preferences(prefs)) saved = false;
  } else {
    if (!save_ratio_and_cave_preferences(prefs)) saved = false;
    if (!save_general_preferences(prefs)) saved = false;
  }
  prefs.end();
  if (configurationIsDirty()) saved = false;
  return saved;
}

void save_stuff() {
  Serial.printf("Saving prefs\n");
  if (!save_all_preferences()) Serial.println("Failed to save one or more preferences");
}

void request_preferences_save() {
  preferences_save_requested = true;
}

void service_preferences_save() {
  if (!preferences_save_requested) return;
  // preferences_save_requested 是 volatile bool，单字节读写在 ESP32 上天然原子，
  // noInterrupts() 无法屏蔽 FreeRTOS 任务级抢占，移除无效临界区。
  preferences_save_requested = false;
  save_stuff();
}

bool store_unq_caves_to_prefs(bool prefs_prepped) {
  if (!prefs_prepped && !prefs.begin("settings", RW_MODE)) return false;

  bool saved = save_unquantized_cave_preferences(prefs);
  if (!prefs_prepped) {
    if (saved && prefs.putBool("unq_caves_store", unq_caves_store)) {
      Serial.printf("unq_caves_store is saved, value is %d\n", unq_caves_store);
    } else {
      saved = false;
      for (int ch = 0; ch < 2; ch++) {
        for (int cave = 0; cave < 3; cave++) unquantized_cave_config_dirty[ch][cave] = true;
      }
      Serial.println("Failed to save unq_caves_store to prefs");
    }
    prefs.end();
  }
  return saved;
}
