#include <stdarg.h>

void apply_ratio_profile_to_dsp();
bool save_all_preferences();
#if MIDI_DIAGNOSTICS
void serviceMidiDiagnosticsByte(char value);
#endif

namespace {

const char *kRatioProfileKey = "ratio_profile";
char serialConfigFrame[wingie_serial::kMaxFrameBytes + 1];
size_t serialConfigLength = 0;
bool serialConfigActive = false;
bool serialConfigOverflow = false;

struct JsonResponse {
  char data[wingie_serial::kMaxFrameBytes + 1];
  size_t length;
  bool valid;

  JsonResponse() : length(0), valid(true) {
    data[0] = '\0';
  }

  void append(const char *format, ...) __attribute__((format(printf, 2, 3))) {
    if (!valid || length >= wingie_serial::kMaxFrameBytes) return;
    va_list arguments;
    va_start(arguments, format);
    const int written = vsnprintf(data + length, sizeof(data) - length, format, arguments);
    va_end(arguments);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(data) - length) {
      valid = false;
      return;
    }
    length += static_cast<size_t>(written);
  }
};

struct CaveBankSnapshot {
  float frequencies[wingie_config::kRatioCount];
  bool mute[wingie_config::kRatioCount];
  uint32_t revision;
  bool dirty;
  bool active;
};

const char *parseErrorCode(wingie_serial::ParseErrorCode code) {
  switch (code) {
    case wingie_serial::kParseUnsupportedVersion:
      return "unsupported_version";
    case wingie_serial::kParseUnknownOperation:
      return "unknown_operation";
    case wingie_serial::kParseFrameTooLarge:
      return "frame_too_large";
    default:
      return "invalid_json";
  }
}

void writeProtocolLine(const char *data, size_t length) {
  char frame[wingie_serial::kMaxFrameBytes + 3];
  frame[0] = '<';
  memcpy(frame + 1, data, length);
  frame[length + 1] = '\n';
  Serial.write(reinterpret_cast<const uint8_t *>(frame), length + 2);
}

void sendJson(JsonResponse &response) {
  if (!response.valid) {
    const char fallback[] = "{\"v\":1,\"id\":0,\"ok\":false,\"error\":{\"code\":\"response_too_large\"}}";
    writeProtocolLine(fallback, sizeof(fallback) - 1);
    return;
  }
  writeProtocolLine(response.data, response.length);
}

void sendError(uint32_t id, const char *code, const char *field, const char *message) {
  JsonResponse response;
  response.append("{\"v\":1,\"id\":%lu,\"ok\":false,\"error\":{\"code\":\"%s\"",
                  static_cast<unsigned long>(id), code);
  if (field) response.append(",\"field\":\"%s\"", field);
  if (message) response.append(",\"message\":\"%s\"", message);
  response.append("}}");
  sendJson(response);
}

void appendRatioArray(JsonResponse &response, const float *ratios) {
  response.append("[");
  for (uint8_t index = 0; index < wingie_config::kRatioCount; index++) {
    response.append(index ? ",%.3f" : "%.3f", ratios[index]);
  }
  response.append("]");
}

void appendCaveFrequencyArray(JsonResponse &response, const float *frequencies) {
  response.append("[");
  for (uint8_t index = 0; index < wingie_config::kRatioCount; index++) {
    response.append(index ? ",%.2f" : "%.2f", frequencies[index]);
  }
  response.append("]");
}

void appendCaveMuteArray(JsonResponse &response, const bool *mute) {
  response.append("[");
  for (uint8_t index = 0; index < wingie_config::kRatioCount; index++) {
    response.append(index ? ",%s" : "%s", mute[index] ? "true" : "false");
  }
  response.append("]");
}

byte caveSideIndex(const char *side) {
  return strcmp(side, "right") == 0 ? 1 : 0;
}

byte activeCaveBank(byte ch) {
  return static_cast<byte>(max(0, min(2, oct[ch] + 1)));
}

void caveStorageKey(char *key, size_t capacity, byte ch, byte bank, bool unquantized) {
  snprintf(key, capacity, unquantized ? "cu2_%c_%u" : "cv2_%c_%u", ch ? 'r' : 'l', bank);
}

bool loadCaveBankV2(Preferences &store, byte ch, byte bank, bool unquantized) {
  char key[12];
  caveStorageKey(key, sizeof(key), ch, bank, unquantized);
  if (store.getBytesLength(key) != sizeof(wingie_config::CaveBankStorage)) return false;

  wingie_config::CaveBankStorage storage;
  if (store.getBytes(key, &storage, sizeof(storage)) != sizeof(storage)) return false;
  wingie_config::CaveBankState decoded;
  if (!wingie_config::decodeCaveBank(storage, decoded)) return false;

  uint16_t legacyFrequencies[wingie_config::kRatioCount];
  bool legacyMute[wingie_config::kRatioCount] = {};
  for (byte index = 0; index < wingie_config::kRatioCount; index++) {
    legacyFrequencies[index] = static_cast<uint16_t>(lroundf(
        unquantized ? cm_freq_stored_unq[ch][bank][index] : cm_freq[ch][bank][index]));
    if (!unquantized) legacyMute[index] = cm_ms[ch][bank][index];
  }
  if (!wingie_config::caveBankMatchesLegacy(
          decoded, legacyFrequencies, legacyMute, wingie_config::kRatioCount)) return false;

  for (byte index = 0; index < wingie_config::kRatioCount; index++) {
    if (unquantized) {
      cm_freq_stored_unq[ch][bank][index] = decoded.frequencies[index];
    } else {
      cm_freq[ch][bank][index] = decoded.frequencies[index];
      cm_freq_prev[ch][bank][index] = decoded.frequencies[index];
      cm_ms[ch][bank][index] = decoded.mute[index];
      cm_ms_prev[ch][bank][index] = decoded.mute[index];
    }
  }
  if (unquantized) {
    unquantized_cave_config_dirty[ch][bank] = false;
    unquantized_cave_storage_migration_pending[ch][bank] = false;
  } else {
    cave_config_revision[ch][bank] = decoded.revision;
    cave_config_dirty[ch][bank] = false;
    cave_storage_migration_pending[ch][bank] = false;
  }
  return true;
}

bool saveCaveBankV2(Preferences &store, byte ch, byte bank, bool unquantized,
                    const wingie_config::CaveBankState &state) {
  wingie_config::CaveBankStorage encoded;
  if (!wingie_config::encodeCaveBank(state, encoded)) return false;
  char key[12];
  caveStorageKey(key, sizeof(key), ch, bank, unquantized);
  if (store.putBytes(key, &encoded, sizeof(encoded)) != sizeof(encoded)) return false;

  wingie_config::CaveBankStorage verified;
  if (store.getBytes(key, &verified, sizeof(verified)) != sizeof(verified)) return false;
  wingie_config::CaveBankState decoded;
  if (!wingie_config::decodeCaveBank(verified, decoded) || decoded.revision != state.revision) return false;
  for (byte index = 0; index < wingie_config::kRatioCount; index++) {
    if (fabsf(decoded.frequencies[index] - state.frequencies[index]) > 0.0051f ||
        decoded.mute[index] != state.mute[index]) return false;
  }
  return true;
}

bool generalSettingsAreDirty() {
  if (tuning_preferences_dirty) return true;
  for (byte index = 0; index < 11; index++) {
    if (dirty[index]) return true;
  }
  return false;
}

bool configurationIsDirty() {
  if (generalSettingsAreDirty() || ratio_profile.dirty) return true;
  for (byte ch = 0; ch < 2; ch++) {
    for (byte bank = 0; bank < wingie_config::kCaveBankCount; bank++) {
      if (cave_config_dirty[ch][bank] || cave_storage_migration_pending[ch][bank]) return true;
      if (unq_caves_store &&
          (unquantized_cave_config_dirty[ch][bank] ||
           unquantized_cave_storage_migration_pending[ch][bank])) return true;
    }
  }
  return false;
}

float clampParameter(float value, float minimum, float maximum) {
  if (value < minimum) return minimum;
  if (value > maximum) return maximum;
  return value;
}

bool quantizeParameter(float value, float minimum, float maximum, float step, float &canonical) {
  if (!isfinite(value)) return false;
  const float clipped = clampParameter(value, minimum, maximum);
  canonical = minimum + roundf((clipped - minimum) / step) * step;
  canonical = clampParameter(canonical, minimum, maximum);
  return true;
}

bool quantizeIntegerParameter(float value, int minimum, int maximum, int &canonical) {
  if (!isfinite(value)) return false;
  if (value <= minimum) canonical = minimum;
  else if (value >= maximum) canonical = maximum;
  else canonical = static_cast<int>(lroundf(value));
  return true;
}

void setWebTuning(int tuning) {
  if (tuning < 0) {
    if (unq_caves_store) restore_caves_to_unq();
    unq_caves_store = false;
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
    alt_tuning_index = tuning;
    dsp.setParamValue("use_alt_tuning", 1);
    alt_tuning_set(tuning);
    tune_caves();
  }
  tuning_preferences_dirty = true;
  apply_note_profiles_to_dsp();
}

bool applyScalarParameter(const wingie_serial::Request &request, float &canonical, bool &cavesChanged) {
  if (strcmp(request.target, "left") == 0 || strcmp(request.target, "right") == 0) {
    const byte ch = strcmp(request.target, "right") == 0 ? 1 : 0;
    if (strcmp(request.name, "mode") == 0) {
      int mode = POLY_MODE;
      if (!quantizeIntegerParameter(request.value, POLY_MODE, RATIO_MODE, mode)) return false;
      if (Mode[ch] != mode) {
        Mode[ch] = mode;
        apply_channel_mode_change(ch);
      }
      canonical = Mode[ch];
      return true;
    }

    int performanceIndex = -1;
    const char *parameterName = nullptr;
    float minimum = 0.0f;
    float maximum = 1.0f;
    if (strcmp(request.name, "mix") == 0) {
      performanceIndex = MIX;
      parameterName = ch ? "mix1" : "mix0";
    } else if (strcmp(request.name, "decay") == 0) {
      performanceIndex = DECAY;
      parameterName = ch ? "/Wingie/right/decay" : "/Wingie/left/decay";
      minimum = 0.1f;
      maximum = 10.0f;
    } else if (strcmp(request.name, "volume") == 0) {
      performanceIndex = VOL;
      parameterName = ch ? "volume1" : "volume0";
    }
    if (performanceIndex >= 0) {
      if (!isfinite(request.value)) return false;
      canonical = clampParameter(request.value, minimum, maximum);
      potValSampled[performanceIndex] = potValRealtime[performanceIndex];
      realtime_value_valid[performanceIndex] = false;
      dsp.setParamValue(parameterName, canonical);
      return true;
    }

    if (strcmp(request.name, "threshold") == 0) {
      if (!quantizeParameter(request.value, 0.0825f, 0.99f, 0.0825f, canonical)) return false;
      float &threshold = ch ? right_thresh : left_thresh;
      if (fabsf(threshold - canonical) > 0.0001f) {
        threshold = canonical;
        dsp.setParamValue(ch ? "right_thresh" : "left_thresh", canonical);
        dirty[4 + ch] = true;
      }
      return true;
    }
    return false;
  }

  if (strcmp(request.target, "shared") != 0) return false;
  if (strcmp(request.name, "a3_hz") == 0) {
    if (!quantizeParameter(request.value, 358.08f, 521.91f, 0.01f, canonical)) return false;
    if (fabsf(a3_freq - canonical) > 0.0051f) {
      a3_freq = canonical;
      dsp.setParamValue("a3_freq", canonical);
      if (use_alt_tuning && alt_tuning_index >= 0) {
        cavesChanged = tune_caves();
      }
      apply_note_profiles_to_dsp();
      dirty[3] = true;
    }
    return true;
  }
  if (strcmp(request.name, "tuning") == 0) {
    int tuning = -1;
    if (!quantizeIntegerParameter(request.value, -1, 7, tuning)) return false;
    if ((use_alt_tuning ? alt_tuning_index : -1) != tuning) {
      setWebTuning(tuning);
      cavesChanged = true;
    }
    canonical = use_alt_tuning ? alt_tuning_index : -1;
    return true;
  }
  if (strcmp(request.name, "pre_clip_gain") == 0 || strcmp(request.name, "post_clip_gain") == 0) {
    const bool post = strcmp(request.name, "post_clip_gain") == 0;
    const float minimum = post ? 0.385f : 0.0825f;
    const float step = post ? 0.055f : 0.0825f;
    if (!quantizeParameter(request.value, minimum, 0.99f, step, canonical)) return false;
    float &gain = post ? post_clip_gain : pre_clip_gain;
    if (fabsf(gain - canonical) > 0.0001f) {
      gain = canonical;
      dsp.setParamValue(post ? "post_clip_gain" : "pre_clip_gain", canonical);
      dirty[post ? 7 : 6] = true;
    }
    return true;
  }

  const char *midiNames[3] = {"midi_left", "midi_right", "midi_both"};
  int *midiValues[3] = {&midi_ch_l, &midi_ch_r, &midi_ch_both};
  for (byte index = 0; index < 3; index++) {
    if (strcmp(request.name, midiNames[index]) != 0) continue;
    int midiValue = 1;
    if (!quantizeIntegerParameter(request.value, 1, 16, midiValue)) return false;
    if (*midiValues[index] != midiValue) {
      *midiValues[index] = midiValue;
      dirty[index] = true;
    }
    canonical = *midiValues[index];
    return true;
  }
  return false;
}

void appendChannelSettings(JsonResponse &response, byte ch) {
  response.append("{\"mode\":%d,\"mix\":%.4f,\"decay\":%.4f,\"volume\":%.4f,\"threshold\":%.4f}",
                  Mode[ch], dsp.getParamValue(ch ? "mix1" : "mix0"),
                  dsp.getParamValue(ch ? "/Wingie/right/decay" : "/Wingie/left/decay"),
                  dsp.getParamValue(ch ? "volume1" : "volume0"), ch ? right_thresh : left_thresh);
}

void sendSettings(uint32_t id) {
  JsonResponse response;
  response.append("{\"v\":1,\"id\":%lu,\"ok\":true,\"op\":\"get_settings\","
                  "\"source\":\"%s\",\"dirty\":%s,\"left\":",
                  static_cast<unsigned long>(id), source ? "line" : "mic",
                  generalSettingsAreDirty() ? "true" : "false");
  appendChannelSettings(response, 0);
  response.append(",\"right\":");
  appendChannelSettings(response, 1);
  response.append(",\"shared\":{\"a3_hz\":%.2f,\"tuning\":%d,"
                  "\"pre_clip_gain\":%.4f,\"post_clip_gain\":%.4f,"
                  "\"midi\":{\"left\":%d,\"right\":%d,\"both\":%d}}}",
                  a3_freq, use_alt_tuning ? alt_tuning_index : -1,
                  pre_clip_gain, post_clip_gain, midi_ch_l, midi_ch_r, midi_ch_both);
  sendJson(response);
}

void sendParameterResponse(uint32_t id, float value, bool cavesChanged) {
  JsonResponse response;
  response.append("{\"v\":1,\"id\":%lu,\"ok\":true,\"op\":\"set_param\","
                  "\"value\":%.4f,\"dirty\":%s,\"caves_changed\":%s}",
                  static_cast<unsigned long>(id), value,
                  generalSettingsAreDirty() ? "true" : "false", cavesChanged ? "true" : "false");
  sendJson(response);
}

void sendHello(uint32_t id) {
  JsonResponse response;
  response.append("{\"v\":1,\"id\":%lu,\"ok\":true,\"op\":\"hello\","
                  "\"device\":\"Wingie2\","
                  "\"capabilities\":[\"settings\",\"ratio_mode\",\"cave_config\",\"mpe\"],"
                  "\"config_schema\":4,\"transport\":{\"baud\":115200,\"max_frame\":%u}}",
                  static_cast<unsigned long>(id), static_cast<unsigned>(wingie_serial::kMaxFrameBytes));
  sendJson(response);
}

void sendRatioProfile(uint32_t id) {
  JsonResponse response;
  response.append("{\"v\":1,\"id\":%lu,\"ok\":true,\"op\":\"get\",\"profile\":{\"ratios\":",
                  static_cast<unsigned long>(id));
  appendRatioArray(response, ratio_profile.ratios);
  response.append(",\"revision\":%lu,\"dirty\":%s},\"factory_profile\":{\"ratios\":",
                  static_cast<unsigned long>(ratio_profile.revision), ratio_profile.dirty ? "true" : "false");
  appendRatioArray(response, wingie_config::kDefaultRatios);
  response.append("},\"limits\":{\"min\":%.3f,\"max\":%.3f,\"step\":%.3f,"
                  "\"frequency_min\":%u,\"frequency_max\":%u}}",
                  wingie_config::kRatioMin, wingie_config::kRatioMax, wingie_config::kRatioStep,
                  wingie_config::kRatioFrequencyMin, wingie_config::kRatioFrequencyMax);
  sendJson(response);
}

void sendCaveBank(uint32_t id, byte ch, byte bank) {
  CaveBankSnapshot snapshot;
  noInterrupts();
  for (byte index = 0; index < wingie_config::kRatioCount; index++) {
    snapshot.frequencies[index] = cm_freq[ch][bank][index];
    snapshot.mute[index] = cm_ms[ch][bank][index];
  }
  snapshot.revision = cave_config_revision[ch][bank];
  snapshot.dirty = cave_config_dirty[ch][bank];
  snapshot.active = activeCaveBank(ch) == bank;
  interrupts();

  JsonResponse response;
  response.append("{\"v\":1,\"id\":%lu,\"ok\":true,\"op\":\"get_cave\","
                  "\"side\":\"%s\",\"bank\":%u,\"active\":%s,\"frequencies\":",
                  static_cast<unsigned long>(id), ch ? "right" : "left", bank,
                  snapshot.active ? "true" : "false");
  appendCaveFrequencyArray(response, snapshot.frequencies);
  response.append(",\"mute\":");
  appendCaveMuteArray(response, snapshot.mute);
  response.append(",\"revision\":%lu,\"dirty\":%s,\"limits\":{\"min\":%.2f,\"max\":%.2f,\"step\":%.2f}}",
                  static_cast<unsigned long>(snapshot.revision), snapshot.dirty ? "true" : "false",
                  wingie_config::kCaveFrequencyMin, wingie_config::kCaveFrequencyMax,
                  wingie_config::kCaveFrequencyStep);
  sendJson(response);
}

void sendStatus(uint32_t id) {
  const int leftNote = currentNote[0];
  const int rightNote = currentNote[1];
  JsonResponse response;
  response.append("{\"v\":1,\"id\":%lu,\"ok\":true,\"op\":\"status\","
                  "\"mode\":{\"left\":%d,\"right\":%d},"
                  "\"note\":{\"left\":%d,\"right\":%d},"
                  "\"fundamental_hz\":{\"left\":%.3f,\"right\":%.3f},"
                  "\"profile_revision\":%lu,\"cave_active_bank\":{\"left\":%u,\"right\":%u}}",
                  static_cast<unsigned long>(id), Mode[0], Mode[1], leftNote, rightNote,
                  configured_note_frequency(leftNote), configured_note_frequency(rightNote),
                  static_cast<unsigned long>(ratio_profile.revision), activeCaveBank(0), activeCaveBank(1));
  sendJson(response);
}

void sendQueued(uint32_t id, const char *operation, uint32_t revision) {
  JsonResponse response;
  response.append("{\"v\":1,\"id\":%lu,\"ok\":true,\"op\":\"%s\",\"state\":\"queued\",\"revision\":%lu}",
                  static_cast<unsigned long>(id), operation, static_cast<unsigned long>(revision));
  sendJson(response);
}

void applyCaveBank(byte ch, byte bank) {
  if (Mode[ch] != CAVE_MODE || activeCaveBank(ch) != bank) return;
  apply_cave_bank_to_dsp(ch, bank);
}

void processSerialConfigFrame() {
  wingie_serial::Request request;
  memset(&request, 0, sizeof(request));
  wingie_serial::ParseError parseError;
  if (!wingie_serial::parseRequestLine(serialConfigFrame, serialConfigLength, request, parseError)) {
    sendError(request.id, parseErrorCode(parseError.code), nullptr, nullptr);
    return;
  }
  if (!serial_config_ready) {
    sendError(request.id, "busy", nullptr, "configuration is still loading");
    return;
  }

  switch (request.operation) {
    case wingie_serial::kOperationHello:
      sendHello(request.id);
      return;
    case wingie_serial::kOperationGet:
      sendRatioProfile(request.id);
      return;
    case wingie_serial::kOperationGetSettings:
      sendSettings(request.id);
      return;
    case wingie_serial::kOperationStatus:
      sendStatus(request.id);
      return;
    case wingie_serial::kOperationGetCave: {
      const byte ch = caveSideIndex(request.side);
      sendCaveBank(request.id, ch, request.bank);
      return;
    }
    case wingie_serial::kOperationSet: {
      if (request.hasExpectedRevision && request.expectedRevision != ratio_profile.revision) {
        sendError(request.id, "revision_conflict", "expected_revision", nullptr);
        return;
      }
      if (!wingie_config::validateRatios(request.ratios, wingie_config::kRatioCount)) {
        sendError(request.id, "invalid_ratio", "ratios", nullptr);
        return;
      }
      for (uint8_t index = 0; index < wingie_config::kRatioCount; index++) {
        uint32_t ratioMilli = 0;
        wingie_config::ratioToMilli(request.ratios[index], ratioMilli);
        ratio_profile.ratios[index] = static_cast<float>(ratioMilli) / wingie_config::kRatioScale;
      }
      ratio_profile.revision++;
      ratio_profile.dirty = true;
      apply_ratio_profile_to_dsp();
      sendQueued(request.id, "set", ratio_profile.revision);
      return;
    }
    case wingie_serial::kOperationSetCave: {
      const byte ch = caveSideIndex(request.side);
      noInterrupts();
      const uint32_t currentRevision = cave_config_revision[ch][request.bank];
      interrupts();
      if (request.hasExpectedRevision && request.expectedRevision != currentRevision) {
        sendError(request.id, "revision_conflict", "expected_revision", nullptr);
        return;
      }
      if (!wingie_config::validateCaveBank(request.frequencies, request.mute, wingie_config::kRatioCount)) {
        sendError(request.id, "invalid_cave_bank", "frequencies", nullptr);
        return;
      }
      float frequencies[wingie_config::kRatioCount];
      for (uint8_t index = 0; index < wingie_config::kRatioCount; index++) {
        uint32_t frequencyCentiHz = 0;
        wingie_config::caveFrequencyToCentiHz(request.frequencies[index], frequencyCentiHz);
        frequencies[index] = static_cast<float>(frequencyCentiHz) / wingie_config::kCaveFrequencyScale;
      }
      bool revisionConflict = false;
      uint32_t revision = 0;
      noInterrupts();
      if (request.hasExpectedRevision && request.expectedRevision != cave_config_revision[ch][request.bank]) {
        revisionConflict = true;
      } else {
        for (uint8_t index = 0; index < wingie_config::kRatioCount; index++) {
          cm_freq[ch][request.bank][index] = frequencies[index];
          cm_ms[ch][request.bank][index] = request.mute[index];
        }
        mark_cave_changed(ch, request.bank);
        applyCaveBank(ch, request.bank);
        revision = cave_config_revision[ch][request.bank];
      }
      interrupts();
      if (revisionConflict) {
        sendError(request.id, "revision_conflict", "expected_revision", nullptr);
        return;
      }
      sendQueued(request.id, "set_cave", revision);
      return;
    }
    case wingie_serial::kOperationSetParam: {
      float canonical = 0.0f;
      bool cavesChanged = false;
      if (!applyScalarParameter(request, canonical, cavesChanged)) {
        sendError(request.id, "invalid_parameter", request.name, nullptr);
        return;
      }
      sendParameterResponse(request.id, canonical, cavesChanged);
      return;
    }
    case wingie_serial::kOperationSave: {
      if (!save_all_preferences()) {
        sendError(request.id, "save_failed", nullptr, nullptr);
        return;
      }
      JsonResponse response;
      response.append("{\"v\":1,\"id\":%lu,\"ok\":true,\"op\":\"save\",\"state\":\"saved\"}",
                      static_cast<unsigned long>(request.id));
      sendJson(response);
      led_blink = 5;
      led_flash_timer = millis();
      return;
    }
    case wingie_serial::kOperationReset: {
      if (request.hasExpectedRevision && request.expectedRevision != ratio_profile.revision) {
        sendError(request.id, "revision_conflict", "expected_revision", nullptr);
        return;
      }
      const uint32_t nextRevision = ratio_profile.revision + 1;
      wingie_config::setFactoryRatios(ratio_profile);
      ratio_profile.revision = nextRevision;
      ratio_profile.dirty = true;
      apply_ratio_profile_to_dsp();
      sendQueued(request.id, "reset", ratio_profile.revision);
      return;
    }
    default:
      sendError(request.id, "unknown_operation", nullptr, nullptr);
      return;
  }
}

}  // namespace

bool load_cave_bank_from_preferences(Preferences &store, byte ch, byte bank, bool unquantized) {
  return loadCaveBankV2(store, ch, bank, unquantized);
}

void apply_ratio_profile_to_dsp() {
  for (byte ch = 0; ch < 2; ch++) {
    if (Mode[ch] == RATIO_MODE) {
      apply_all_ratio_voice_pitch(ch);
    }
  }
}

void load_ratio_profile_from_preferences() {
  wingie_config::setFactoryRatios(ratio_profile);
  if (prefs.getBytesLength(kRatioProfileKey) == sizeof(wingie_config::RatioProfileStorage)) {
    wingie_config::RatioProfileStorage storage;
    if (prefs.getBytes(kRatioProfileKey, &storage, sizeof(storage)) == sizeof(storage)) {
      wingie_config::RatioProfileState loaded;
      if (wingie_config::decodeRatioProfile(storage, loaded)) ratio_profile = loaded;
    }
  }
  apply_ratio_profile_to_dsp();
}

bool save_unquantized_cave_preferences(Preferences &store) {
  bool saved = true;
  for (byte ch = 0; ch < 2; ch++) {
    for (byte bank = 0; bank < wingie_config::kCaveBankCount; bank++) {
      const bool shouldSave = unquantized_cave_config_dirty[ch][bank] ||
                              unquantized_cave_storage_migration_pending[ch][bank];
      if (!shouldSave) continue;
      const bool wasDirty = unquantized_cave_config_dirty[ch][bank];
      const bool wasMigrationPending = unquantized_cave_storage_migration_pending[ch][bank];
      unquantized_cave_config_dirty[ch][bank] = false;
      unquantized_cave_storage_migration_pending[ch][bank] = false;

      wingie_config::CaveBankState snapshot;
      memset(&snapshot, 0, sizeof(snapshot));
      for (byte voice = 0; voice < wingie_config::kRatioCount; voice++) {
        snapshot.frequencies[voice] = cm_freq_stored_unq[ch][bank][voice];
      }

      bool bankSaved = true;
      for (byte voice = 0; voice < wingie_config::kRatioCount; voice++) {
        char key[20];
        snprintf(key, sizeof(key), ch ? "r_cf_unq_%u_%u" : "l_cf_unq_%u_%u", bank, voice);
        const uint16_t legacyFrequency = static_cast<uint16_t>(lroundf(snapshot.frequencies[voice]));
        if (!store.putUShort(key, legacyFrequency)) bankSaved = false;
      }
      if (bankSaved && !saveCaveBankV2(store, ch, bank, true, snapshot)) bankSaved = false;

      bool unchanged = true;
      for (byte voice = 0; voice < wingie_config::kRatioCount; voice++) {
        if (fabsf(cm_freq_stored_unq[ch][bank][voice] - snapshot.frequencies[voice]) > 0.0001f) {
          unchanged = false;
        }
      }
      if (!bankSaved || !unchanged) {
        if (wasDirty || !unchanged) unquantized_cave_config_dirty[ch][bank] = true;
        if (wasMigrationPending) unquantized_cave_storage_migration_pending[ch][bank] = true;
        saved = false;
      }
    }
  }
  return saved;
}

bool save_ratio_and_cave_preferences(Preferences &store) {
  bool saved = true;
  if (ratio_profile.dirty) {
    ratio_profile.dirty = false;
    const wingie_config::RatioProfileState snapshot = ratio_profile;
    wingie_config::RatioProfileStorage storage;
    if (!wingie_config::encodeRatioProfile(snapshot, storage) ||
        store.putBytes(kRatioProfileKey, &storage, sizeof(storage)) != sizeof(storage)) {
      ratio_profile.dirty = true;
      saved = false;
    }
  }

  for (byte ch = 0; ch < 2; ch++) {
    for (byte bank = 0; bank < wingie_config::kCaveBankCount; bank++) {
      const bool shouldSave = cave_config_dirty[ch][bank] || cave_storage_migration_pending[ch][bank];
      if (!shouldSave) continue;
      const bool wasDirty = cave_config_dirty[ch][bank];
      const bool wasMigrationPending = cave_storage_migration_pending[ch][bank];
      cave_config_dirty[ch][bank] = false;
      cave_storage_migration_pending[ch][bank] = false;

      wingie_config::CaveBankState snapshot;
      memset(&snapshot, 0, sizeof(snapshot));
      for (byte voice = 0; voice < wingie_config::kRatioCount; voice++) {
        snapshot.frequencies[voice] = cm_freq[ch][bank][voice];
        snapshot.mute[voice] = cm_ms[ch][bank][voice];
      }
      snapshot.revision = cave_config_revision[ch][bank];

      bool bankSaved = true;
      for (byte voice = 0; voice < wingie_config::kRatioCount; voice++) {
        char key[16];
        snprintf(key, sizeof(key), ch ? "r_cf_%u_%u" : "l_cf_%u_%u", bank, voice);
        const uint16_t legacyFrequency = static_cast<uint16_t>(lroundf(snapshot.frequencies[voice]));
        if (!store.putUShort(key, legacyFrequency)) bankSaved = false;
        snprintf(key, sizeof(key), ch ? "r_cms_%u_%u" : "l_cms_%u_%u", bank, voice);
        if (!store.putBool(key, snapshot.mute[voice])) bankSaved = false;
      }
      if (bankSaved && !saveCaveBankV2(store, ch, bank, false, snapshot)) bankSaved = false;

      bool unchanged = cave_config_revision[ch][bank] == snapshot.revision;
      for (byte voice = 0; voice < wingie_config::kRatioCount; voice++) {
        if (fabsf(cm_freq[ch][bank][voice] - snapshot.frequencies[voice]) > 0.0001f ||
            cm_ms[ch][bank][voice] != snapshot.mute[voice]) {
          unchanged = false;
        }
      }
      if (bankSaved && unchanged) {
        for (byte voice = 0; voice < wingie_config::kRatioCount; voice++) {
          cm_freq_prev[ch][bank][voice] = snapshot.frequencies[voice];
          cm_ms_prev[ch][bank][voice] = snapshot.mute[voice];
        }
      } else {
        if (wasDirty || !unchanged) cave_config_dirty[ch][bank] = true;
        if (wasMigrationPending) cave_storage_migration_pending[ch][bank] = true;
        saved = false;
      }
    }
  }

  return saved;
}

void service_serial_configuration() {
  while (Serial.available()) {
    const char value = static_cast<char>(Serial.read());
    if (serialConfigActive) {
      if (value == '\n') {
        if (!serialConfigOverflow) processSerialConfigFrame();
        else sendError(0, "frame_too_large", nullptr, nullptr);
        serialConfigActive = false;
        serialConfigOverflow = false;
        serialConfigLength = 0;
      } else if (value != '\r') {
        if (serialConfigLength < wingie_serial::kMaxFrameBytes) {
          serialConfigFrame[serialConfigLength++] = value;
          serialConfigFrame[serialConfigLength] = '\0';
        } else {
          serialConfigOverflow = true;
        }
      }
    } else if (value == '@') {
      serialConfigActive = true;
      serialConfigOverflow = false;
      serialConfigLength = 1;
      serialConfigFrame[0] = '@';
      serialConfigFrame[1] = '\0';
    } else {
#if MIDI_DIAGNOSTICS
      serviceMidiDiagnosticsByte(value);
#endif
    }
  }
}
