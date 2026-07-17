bool save_general_preferences(Preferences &store) {
  bool saved = true;
  if (dirty[0]) {
    dirty[0] = false;
    const int value = midi_ch_l;
    if (store.putUChar("midi_ch_l", value)) Serial.printf("midi_ch_l is saved, value is %d.\n", value);
    else {
      dirty[0] = true;
      saved = false;
    }
  }
  if (dirty[1]) {
    dirty[1] = false;
    const int value = midi_ch_r;
    if (store.putUChar("midi_ch_r", value)) Serial.printf("midi_ch_r is saved, value is %d.\n", value);
    else {
      dirty[1] = true;
      saved = false;
    }
  }
  if (dirty[2]) {
    dirty[2] = false;
    const int value = midi_ch_both;
    if (store.putUChar("midi_ch_both", value)) Serial.printf("midi_ch_both is saved, value is %d.\n", value);
    else {
      dirty[2] = true;
      saved = false;
    }
  }

  if (dirty[3]) {
    dirty[3] = false;
    const float frequency = a3_freq;
    const float freq_offset = frequency - 440.;
    if (store.putFloat("a3_freq_offset", freq_offset)) {
      Serial.printf("a3_freq_offset (%.2fHz) is saved. a3 = %.2fHz.\n", freq_offset, frequency);
    } else {
      dirty[3] = true;
      saved = false;
    }
  }

  if (dirty[4]) {
    dirty[4] = false;
    const float value = left_thresh;
    if (store.putFloat("left_thresh", value)) Serial.printf("left_thresh is saved, value is %.4f\n", value);
    else {
      dirty[4] = true;
      saved = false;
    }
  }
  if (dirty[5]) {
    dirty[5] = false;
    const float value = right_thresh;
    if (store.putFloat("right_thresh", value)) Serial.printf("right_thresh is saved, value is %.4f\n", value);
    else {
      dirty[5] = true;
      saved = false;
    }
  }
  if (dirty[6]) {
    dirty[6] = false;
    const float value = pre_clip_gain;
    if (store.putFloat("pre_clip_gain", value)) Serial.printf("pre_clip_gain is saved, value is %.4f\n", value);
    else {
      dirty[6] = true;
      saved = false;
    }
  }
  if (dirty[7]) {
    dirty[7] = false;
    const float value = post_clip_gain;
    if (store.putFloat("post_clip_gain", value)) Serial.printf("post_clip_gain is saved, value is %.4f\n", value);
    else {
      dirty[7] = true;
      saved = false;
    }
  }

  if (dirty[8]) {
    dirty[8] = false;
    const int value = Mode[0];
    if (store.putUChar("left_mode", value)) Serial.printf("left_mode is saved, value is %d.\n", value);
    else {
      dirty[8] = true;
      saved = false;
    }
  }
  if (dirty[9]) {
    dirty[9] = false;
    const int value = Mode[1];
    if (store.putUChar("right_mode", value)) Serial.printf("right_mode is saved, value is %d.\n", value);
    else {
      dirty[9] = true;
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
  noInterrupts();
  preferences_save_requested = false;
  interrupts();
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
