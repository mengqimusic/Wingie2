void apply_ratio_profile_to_dsp();

float channel_mix[2] = {1.0f, 1.0f};
float channel_decay[2] = {0.1f, 0.1f};
float channel_volume[2] = {0.0f, 0.0f};
bool tuning_dirty = false;
uint32_t tuning_revision = 0;
uint32_t config_boot_id = 0;
volatile uint32_t config_state_revision = 0;
volatile bool config_event_pending = false;
volatile ConfigOrigin config_last_origin = CONFIG_ORIGIN_STARTUP;

namespace {

SemaphoreHandle_t configStateMutex = nullptr;

float clampFloat(float value, float minimum, float maximum) {
  return max(minimum, min(maximum, value));
}

float quantizeFloat(float value, float minimum, float maximum, float step) {
  const float clipped = clampFloat(value, minimum, maximum);
  return minimum + roundf((clipped - minimum) / step) * step;
}

bool differs(float first, float second, float epsilon) {
  return fabsf(first - second) > epsilon;
}

void applyActiveCaves() {
  for (uint8_t channel = 0; channel < 2; channel++) {
    if (Mode[channel] == CAVE_MODE) apply_current_mode_parameters(channel);
  }
}

}  // namespace

void initialize_config_runtime() {
  if (!configStateMutex) configStateMutex = xSemaphoreCreateRecursiveMutex();
  config_boot_id = esp_random();
  if (!config_boot_id) config_boot_id = 1;
}

void lock_config_state() {
  if (configStateMutex) xSemaphoreTakeRecursive(configStateMutex, portMAX_DELAY);
}

void unlock_config_state() {
  if (configStateMutex) xSemaphoreGiveRecursive(configStateMutex);
}

void mark_config_state_changed(ConfigOrigin origin) {
  lock_config_state();
  config_last_origin = origin;
  config_state_revision++;
  config_event_pending = true;
  unlock_config_state();
}

bool configuration_is_dirty() {
  lock_config_state();
  bool stateDirty = ratio_profile.dirty || tuning_dirty || unquantized_caves_dirty ||
                    cave_storage_migration_pending;
  for (uint8_t index = 0; index < 10; index++) {
    if (dirty[index]) stateDirty = true;
  }
  for (uint8_t channel = 0; channel < 2; channel++) {
    for (uint8_t bank = 0; bank < wingie_config::kCaveBankCount; bank++) {
      if (cave_config_dirty[channel][bank]) stateDirty = true;
    }
  }
  unlock_config_state();
  return stateDirty;
}

bool set_channel_mode(uint8_t channel, int value, ConfigOrigin origin) {
  if (channel > 1) return false;
  const int canonical = max(POLY_MODE, min(RATIO_MODE, value));
  lock_config_state();
  if (Mode[channel] == canonical) {
    unlock_config_state();
    return true;
  }

  Mode[channel] = canonical;
  set_channel_dsp_mode(channel);
  dsp.setParamValue(channel ? "/Wingie/right/mode_changed" : "/Wingie/left/mode_changed", 1);
  duck_env_triggered[channel] = true;
  duck_env_init_timer[channel] = millis();
  dirty[8 + channel] = true;
  ratio_led_phase[channel] = 0;
  ratio_led_timer[channel] = millis();
  set_mode_led(channel);
  apply_current_mode_parameters(channel);
  mark_config_state_changed(origin);
  unlock_config_state();
  return true;
}

bool set_channel_performance_parameter(uint8_t channel, PerformanceParameter parameter,
                                       float value, ConfigOrigin origin, bool remote_takeover) {
  if (channel > 1 || !isfinite(value)) return false;
  lock_config_state();
  if (remote_takeover) {
    realtime_value_valid[parameter] = false;
    potValSampled[parameter] = potValRealtime[parameter];
  }

  float canonical = value;
  float *state = nullptr;
  const char *parameterName = nullptr;
  float notificationEpsilon = origin == CONFIG_ORIGIN_HARDWARE ? 0.001f : 0.000001f;
  if (parameter == PERFORMANCE_MIX) {
    canonical = clampFloat(value, 0.0f, 1.0f);
    state = &channel_mix[channel];
    parameterName = channel ? "mix1" : "mix0";
  } else if (parameter == PERFORMANCE_DECAY) {
    canonical = clampFloat(value, 0.1f, 10.0f);
    state = &channel_decay[channel];
    parameterName = channel ? "/Wingie/right/decay" : "/Wingie/left/decay";
    notificationEpsilon = origin == CONFIG_ORIGIN_HARDWARE ? 0.01f : 0.000001f;
  } else if (parameter == PERFORMANCE_VOLUME) {
    canonical = clampFloat(value, 0.0f, 1.0f);
    state = &channel_volume[channel];
    parameterName = channel ? "volume1" : "volume0";
  } else {
    unlock_config_state();
    return false;
  }

  dsp.setParamValue(parameterName, canonical);
  if (!differs(*state, canonical, notificationEpsilon)) {
    unlock_config_state();
    return true;
  }
  *state = canonical;
  mark_config_state_changed(origin);
  unlock_config_state();
  return true;
}

bool set_channel_threshold(uint8_t channel, float value, ConfigOrigin origin) {
  if (channel > 1 || !isfinite(value)) return false;
  lock_config_state();
  const float canonical = quantizeFloat(value, 0.0825f, 0.99f, 0.0825f);
  float &state = channel ? right_thresh : left_thresh;
  if (!differs(state, canonical, 0.00001f)) {
    unlock_config_state();
    return true;
  }
  state = canonical;
  dsp.setParamValue(channel ? "right_thresh" : "left_thresh", canonical);
  dirty[4 + channel] = true;
  mark_config_state_changed(origin);
  unlock_config_state();
  return true;
}

bool set_clip_gain(bool post, float value, ConfigOrigin origin) {
  if (!isfinite(value)) return false;
  lock_config_state();
  const float minimum = post ? 0.385f : 0.0825f;
  const float step = post ? 0.055f : 0.0825f;
  const float canonical = quantizeFloat(value, minimum, 0.99f, step);
  float &state = post ? post_clip_gain : pre_clip_gain;
  if (!differs(state, canonical, 0.00001f)) {
    unlock_config_state();
    return true;
  }
  state = canonical;
  dsp.setParamValue(post ? "post_clip_gain" : "pre_clip_gain", canonical);
  dirty[post ? 7 : 6] = true;
  mark_config_state_changed(origin);
  unlock_config_state();
  return true;
}

bool set_a3_frequency(float value, ConfigOrigin origin) {
  if (!isfinite(value)) return false;
  lock_config_state();
  const float canonical = quantizeFloat(value, 358.08f, 521.91f, 0.01f);
  if (!differs(a3_freq, canonical, 0.0001f)) {
    unlock_config_state();
    return true;
  }
  a3_freq = canonical;
  dsp.setParamValue("a3_freq", canonical);
  if (use_alt_tuning && alt_tuning_index >= 0) {
    tune_caves(origin);
    applyActiveCaves();
  }
  apply_note_profiles_to_dsp();
  dirty[3] = true;
  mark_config_state_changed(origin);
  unlock_config_state();
  return true;
}

bool set_tuning_index(int value, ConfigOrigin origin) {
  const int canonical = max(-1, min(7, value));
  lock_config_state();
  if (alt_tuning_index == canonical && use_alt_tuning == (canonical >= 0)) {
    unlock_config_state();
    return true;
  }

  if (canonical < 0) {
    if (unq_caves_store) restore_caves_to_unq(origin);
    unq_caves_store = false;
    unquantized_caves_dirty = false;
    use_alt_tuning = 0;
    alt_tuning_index = -1;
    alt_tuning_set(-1);
    dsp.setParamValue("use_alt_tuning", 0);
  } else {
    if (!use_alt_tuning) {
      store_unq_caves();
      unq_caves_store = true;
    }
    use_alt_tuning = 1;
    alt_tuning_index = canonical;
    dsp.setParamValue("use_alt_tuning", 1);
    alt_tuning_set(canonical);
    tune_caves(origin);
  }
  apply_note_profiles_to_dsp();
  applyActiveCaves();
  tuning_revision++;
  tuning_dirty = true;
  mark_config_state_changed(origin);
  unlock_config_state();
  return true;
}

bool set_midi_channel(uint8_t slot, int value, ConfigOrigin origin) {
  if (slot > 2) return false;
  lock_config_state();
  const int canonical = max(1, min(16, value));
  int *channels[3] = {&midi_ch_l, &midi_ch_r, &midi_ch_both};
  if (*channels[slot] == canonical) {
    unlock_config_state();
    return true;
  }
  *channels[slot] = canonical;
  dirty[slot] = true;
  mark_config_state_changed(origin);
  unlock_config_state();
  return true;
}

bool set_ratio_value(uint8_t index, float value, ConfigOrigin origin, float *canonicalValue) {
  if (index >= wingie_config::kRatioCount) return false;
  float canonical = 0.0f;
  if (!wingie_config::canonicalizeRatio(value, canonical)) return false;
  lock_config_state();
  if (canonicalValue) *canonicalValue = canonical;
  if (!differs(ratio_profile.ratios[index], canonical, 0.000001f)) {
    unlock_config_state();
    return true;
  }
  ratio_profile.ratios[index] = canonical;
  ratio_profile.revision++;
  ratio_profile.dirty = true;
  apply_ratio_profile_to_dsp();
  mark_config_state_changed(origin);
  unlock_config_state();
  return true;
}

bool set_cave_frequency(uint8_t channel, uint8_t bank, uint8_t index, float value,
                        ConfigOrigin origin, float *canonicalValue) {
  if (channel > 1 || bank >= wingie_config::kCaveBankCount || index >= wingie_config::kRatioCount) return false;
  float canonical = 0.0f;
  if (!wingie_config::canonicalizeCaveFrequency(value, canonical)) return false;
  lock_config_state();
  if (canonicalValue) *canonicalValue = canonical;
  if (!differs(cm_freq[channel][bank][index], canonical, 0.0001f)) {
    unlock_config_state();
    return true;
  }
  cm_freq[channel][bank][index] = canonical;
  mark_cave_changed(channel, bank, origin);
  if (Mode[channel] == CAVE_MODE && bank == oct[channel] + 1) cm_freq_set(channel, index, canonical);
  unlock_config_state();
  return true;
}

bool set_cave_mute(uint8_t channel, uint8_t bank, uint8_t index, bool value, ConfigOrigin origin) {
  if (channel > 1 || bank >= wingie_config::kCaveBankCount || index >= wingie_config::kRatioCount) return false;
  lock_config_state();
  if (cm_ms[channel][bank][index] == value) {
    unlock_config_state();
    return true;
  }
  cm_ms[channel][bank][index] = value;
  mark_cave_changed(channel, bank, origin);
  if (Mode[channel] == CAVE_MODE && bank == oct[channel] + 1) cm_mute_set(channel, index, value);
  unlock_config_state();
  return true;
}
