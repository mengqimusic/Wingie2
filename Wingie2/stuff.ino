namespace {

bool configurationSaveInProgress = false;

void finishConfigurationSave() {
  lock_config_state();
  configurationSaveInProgress = false;
  unlock_config_state();
}

}  // namespace

bool save_all_configuration() {
  lock_config_state();
  if (configurationSaveInProgress) {
    unlock_config_state();
    return false;
  }
  configurationSaveInProgress = true;

  const bool tuningDirtyAtStart = tuning_dirty;
  const uint32_t tuningRevision = tuning_revision;
  const int tuningEnabled = use_alt_tuning;
  const int tuningIndex = alt_tuning_index;
  const bool unquantizedStored = unq_caves_store;
  const bool saveUnquantizedBanks = unquantizedStored &&
                                      (unquantized_caves_dirty || cave_storage_migration_pending);
  unlock_config_state();

  Serial.println("Saving prefs");
  if (!prefs.begin("settings", RW_MODE)) {
    finishConfigurationSave();
    return false;
  }

  bool currentCavesSaved = true;
  bool unquantizedCavesSaved = true;
  bool saved = save_ratio_and_cave_preferences(
      prefs, saveUnquantizedBanks, tuningRevision, currentCavesSaved, unquantizedCavesSaved);

  const char *midiKeys[3] = {"midi_ch_l", "midi_ch_r", "midi_ch_both"};
  int *midiChannels[3] = {&midi_ch_l, &midi_ch_r, &midi_ch_both};
  for (uint8_t index = 0; index < 3; index++) {
    lock_config_state();
    const bool shouldSave = dirty[index];
    const int value = *midiChannels[index];
    unlock_config_state();
    if (!shouldSave) continue;
    if (prefs.putUChar(midiKeys[index], value)) {
      lock_config_state();
      if (*midiChannels[index] == value) dirty[index] = false;
      else {
        saved = false;
      }
      unlock_config_state();
    } else {
      saved = false;
    }
  }

  lock_config_state();
  const bool saveA3 = dirty[3];
  const float a3Value = a3_freq;
  unlock_config_state();
  if (saveA3) {
    if (prefs.putFloat("a3_freq_offset", a3Value - 440.0f)) {
      lock_config_state();
      if (a3_freq == a3Value) dirty[3] = false;
      else saved = false;
      unlock_config_state();
    } else {
      saved = false;
    }
  }

  const char *thresholdKeys[2] = {"left_thresh", "right_thresh"};
  float *thresholds[2] = {&left_thresh, &right_thresh};
  for (uint8_t channel = 0; channel < 2; channel++) {
    lock_config_state();
    const bool shouldSave = dirty[4 + channel];
    const float value = *thresholds[channel];
    unlock_config_state();
    if (!shouldSave) continue;
    if (prefs.putFloat(thresholdKeys[channel], value)) {
      lock_config_state();
      if (*thresholds[channel] == value) dirty[4 + channel] = false;
      else saved = false;
      unlock_config_state();
    } else {
      saved = false;
    }
  }

  lock_config_state();
  const bool savePreGain = dirty[6];
  const float preGainValue = pre_clip_gain;
  unlock_config_state();
  if (savePreGain) {
    if (prefs.putFloat("pre_clip_gain", preGainValue)) {
      lock_config_state();
      if (pre_clip_gain == preGainValue) dirty[6] = false;
      else saved = false;
      unlock_config_state();
    } else {
      saved = false;
    }
  }
  lock_config_state();
  const bool savePostGain = dirty[7];
  const float postGainValue = post_clip_gain;
  unlock_config_state();
  if (savePostGain) {
    if (prefs.putFloat("post_clip_gain", postGainValue)) {
      lock_config_state();
      if (post_clip_gain == postGainValue) dirty[7] = false;
      else saved = false;
      unlock_config_state();
    } else {
      saved = false;
    }
  }

  const char *modeKeys[2] = {"left_mode", "right_mode"};
  for (uint8_t channel = 0; channel < 2; channel++) {
    lock_config_state();
    const bool shouldSave = dirty[8 + channel];
    const int value = Mode[channel];
    unlock_config_state();
    if (!shouldSave) continue;
    if (prefs.putUChar(modeKeys[channel], value)) {
      lock_config_state();
      if (Mode[channel] == value) dirty[8 + channel] = false;
      else saved = false;
      unlock_config_state();
    } else {
      saved = false;
    }
  }

  if (tuningDirtyAtStart) {
    const bool backupReady = !unquantizedStored || unquantizedCavesSaved;
    bool tuningSaved = false;
    if (backupReady) {
      tuningSaved = prefs.putUChar("use_alt_tuning", tuningEnabled) &&
                    prefs.putChar("alt_tuning_idx", tuningIndex) &&
                    prefs.putBool("unq_caves_store", unquantizedStored);
    }
    lock_config_state();
    const bool tuningUnchanged = use_alt_tuning == tuningEnabled &&
                                 alt_tuning_index == tuningIndex &&
                                 unq_caves_store == unquantizedStored &&
                                 tuning_revision == tuningRevision;
    if (tuningSaved && tuningUnchanged) {
      tuning_dirty = false;
    } else {
      saved = false;
    }
    unlock_config_state();
  } else {
    lock_config_state();
    if (tuning_dirty) saved = false;
    unlock_config_state();
  }

  prefs.end();
  finishConfigurationSave();
  return saved;
}

void save_stuff() {
  if (save_all_configuration()) mark_config_state_changed(CONFIG_ORIGIN_HARDWARE);
  else Serial.println("Failed to save one or more preferences");
}
