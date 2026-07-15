#include <stdarg.h>

void apply_ratio_profile_to_dsp();
bool save_serial_configuration();
#if MIDI_DIAGNOSTICS
void serviceMidiDiagnosticsByte(char value);
#endif

namespace {

const char *kRatioProfileKey = "ratio_profile";
char serialConfigFrame[wingie_serial::kMaxFrameBytes + 1];
size_t serialConfigLength = 0;
bool serialConfigActive = false;
bool serialConfigOverflow = false;
unsigned long serialConfigEventTimer = 0;

struct JsonResponse {
  char data[wingie_serial::kMaxFrameBytes + 1];
  size_t length;
  bool valid;

  JsonResponse() : length(0), valid(true) {
    data[0] = '\0';
  }

  void append(const char *format, ...) {
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

void appendCaveFrequencyArray(JsonResponse &response, byte ch, byte bank) {
  response.append("[");
  for (uint8_t index = 0; index < wingie_config::kRatioCount; index++) {
    response.append(index ? ",%.2f" : "%.2f", cm_freq[ch][bank][index]);
  }
  response.append("]");
}

void appendCaveMuteArray(JsonResponse &response, byte ch, byte bank) {
  response.append("[");
  for (uint8_t index = 0; index < wingie_config::kRatioCount; index++) {
    response.append(index ? ",%s" : "%s", cm_ms[ch][bank][index] ? "true" : "false");
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
  if (!unquantized) cave_config_revision[ch][bank] = decoded.revision;
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

void sendHello(uint32_t id) {
  JsonResponse response;
  response.append("{\"v\":1,\"id\":%lu,\"ok\":true,\"op\":\"hello\","
                  "\"device\":\"Wingie2\",\"boot_id\":%lu,"
                  "\"capabilities\":[\"realtime_config\",\"ratio_mode\",\"cave_config\"],"
                  "\"config_schema\":2,\"transport\":{\"baud\":115200,\"max_frame\":%u}}",
                  static_cast<unsigned long>(id), static_cast<unsigned long>(config_boot_id),
                  static_cast<unsigned>(wingie_serial::kMaxFrameBytes));
  sendJson(response);
}

void sendRatioProfile(uint32_t id) {
  JsonResponse response;
  lock_config_state();
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
  unlock_config_state();
  sendJson(response);
}

void sendCaveBank(uint32_t id, byte ch, byte bank) {
  JsonResponse response;
  lock_config_state();
  response.append("{\"v\":1,\"id\":%lu,\"ok\":true,\"op\":\"get_cave\","
                  "\"side\":\"%s\",\"bank\":%u,\"active\":%s,\"frequencies\":",
                  static_cast<unsigned long>(id), ch ? "right" : "left", bank,
                  activeCaveBank(ch) == bank ? "true" : "false");
  appendCaveFrequencyArray(response, ch, bank);
  response.append(",\"mute\":");
  appendCaveMuteArray(response, ch, bank);
  response.append(",\"revision\":%lu,\"dirty\":%s,\"limits\":{\"min\":%.2f,\"max\":%.2f,\"step\":%.2f}}",
                  static_cast<unsigned long>(cave_config_revision[ch][bank]),
                  cave_config_dirty[ch][bank] ? "true" : "false",
                  wingie_config::kCaveFrequencyMin, wingie_config::kCaveFrequencyMax,
                  wingie_config::kCaveFrequencyStep);
  unlock_config_state();
  sendJson(response);
}

const char *configOriginName(ConfigOrigin origin) {
  if (origin == CONFIG_ORIGIN_HARDWARE) return "hardware";
  if (origin == CONFIG_ORIGIN_MIDI) return "midi";
  if (origin == CONFIG_ORIGIN_WEB) return "web";
  return "startup";
}

void appendChannelState(JsonResponse &response, byte ch) {
  response.append("{\"mode\":%d,\"octave\":%d,\"active_cave_bank\":%u,"
                  "\"note\":%d,\"fundamental_hz\":%.3f,\"mix\":%.4f,\"decay\":%.4f,"
                  "\"volume\":%.4f,\"threshold\":%.4f,\"trigger\":%s,\"cave_revisions\":[%lu,%lu,%lu]}",
                  Mode[ch], oct[ch], activeCaveBank(ch), currentNote[ch],
                  configured_note_frequency(currentNote[ch]), channel_mix[ch], channel_decay[ch],
                  channel_volume[ch], ch ? right_thresh : left_thresh, trig[ch] ? "true" : "false",
                  static_cast<unsigned long>(cave_config_revision[ch][0]),
                  static_cast<unsigned long>(cave_config_revision[ch][1]),
                  static_cast<unsigned long>(cave_config_revision[ch][2]));
}

void sendConfigurationState(uint32_t id) {
  JsonResponse response;
  lock_config_state();
  response.append("{\"v\":1,\"id\":%lu,\"ok\":true,\"op\":\"get_state\","
                  "\"boot_id\":%lu,\"revision\":%lu,\"dirty\":%s,\"source\":\"%s\",\"shared\":{"
                  "\"a3_hz\":%.2f,\"tuning\":%d,\"pre_clip_gain\":%.4f,"
                  "\"post_clip_gain\":%.4f,\"midi\":{\"left\":%d,\"right\":%d,\"both\":%d}},"
                  "\"left\":",
                  static_cast<unsigned long>(id), static_cast<unsigned long>(config_boot_id),
                  static_cast<unsigned long>(config_state_revision),
                  configuration_is_dirty() ? "true" : "false", source ? "line" : "mic", a3_freq,
                  use_alt_tuning ? alt_tuning_index : -1, pre_clip_gain, post_clip_gain,
                  midi_ch_l, midi_ch_r, midi_ch_both);
  appendChannelState(response, 0);
  response.append(",\"right\":");
  appendChannelState(response, 1);
  response.append(",\"ratio_revision\":%lu}", static_cast<unsigned long>(ratio_profile.revision));
  unlock_config_state();
  sendJson(response);
}

void sendChangedEvent(uint32_t revision, ConfigOrigin origin) {
  JsonResponse response;
  response.append("{\"v\":1,\"event\":\"changed\",\"revision\":%lu,\"origin\":\"%s\"}",
                  static_cast<unsigned long>(revision), configOriginName(origin));
  sendJson(response);
}

void sendValueResponse(uint32_t id, const char *operation, float value, bool clipped,
                       uint32_t resourceRevision) {
  lock_config_state();
  const uint32_t revision = config_state_revision;
  unlock_config_state();
  JsonResponse response;
  response.append("{\"v\":1,\"id\":%lu,\"ok\":true,\"op\":\"%s\","
                  "\"value\":%.4f,\"clipped\":%s,\"revision\":%lu,\"resource_revision\":%lu}",
                  static_cast<unsigned long>(id), operation, value, clipped ? "true" : "false",
                  static_cast<unsigned long>(revision), static_cast<unsigned long>(resourceRevision));
  sendJson(response);
}

void sendMuteResponse(uint32_t id, bool value, uint32_t resourceRevision) {
  lock_config_state();
  const uint32_t revision = config_state_revision;
  unlock_config_state();
  JsonResponse response;
  response.append("{\"v\":1,\"id\":%lu,\"ok\":true,\"op\":\"set_cave_mute\","
                  "\"mute\":%s,\"revision\":%lu,\"resource_revision\":%lu}", value ? "true" : "false",
                  static_cast<unsigned long>(revision), static_cast<unsigned long>(resourceRevision));
  sendJson(response);
}

void sendStatus(uint32_t id) {
  lock_config_state();
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
  unlock_config_state();
  sendJson(response);
}

void sendQueued(uint32_t id, const char *operation, uint32_t revision) {
  JsonResponse response;
  response.append("{\"v\":1,\"id\":%lu,\"ok\":true,\"op\":\"%s\",\"state\":\"applied\",\"revision\":%lu}",
                  static_cast<unsigned long>(id), operation, static_cast<unsigned long>(revision));
  sendJson(response);
}

void applyCaveBank(byte ch, byte bank) {
  if (Mode[ch] != CAVE_MODE || activeCaveBank(ch) != bank) return;
  apply_cave_bank_to_dsp(ch, bank);
}

bool applyScalarParameter(const wingie_serial::Request &request, float &canonical) {
  if (strcmp(request.target, "left") == 0 || strcmp(request.target, "right") == 0) {
    const byte ch = strcmp(request.target, "right") == 0 ? 1 : 0;
    if (strcmp(request.name, "mode") == 0) {
      if (!set_channel_mode(ch, lroundf(request.value), CONFIG_ORIGIN_WEB)) return false;
      canonical = Mode[ch];
      return true;
    }
    if (strcmp(request.name, "mix") == 0) {
      if (!set_channel_performance_parameter(ch, PERFORMANCE_MIX, request.value, CONFIG_ORIGIN_WEB, true)) return false;
      canonical = channel_mix[ch];
      return true;
    }
    if (strcmp(request.name, "decay") == 0) {
      if (!set_channel_performance_parameter(ch, PERFORMANCE_DECAY, request.value, CONFIG_ORIGIN_WEB, true)) return false;
      canonical = channel_decay[ch];
      return true;
    }
    if (strcmp(request.name, "volume") == 0) {
      if (!set_channel_performance_parameter(ch, PERFORMANCE_VOLUME, request.value, CONFIG_ORIGIN_WEB, true)) return false;
      canonical = channel_volume[ch];
      return true;
    }
    if (strcmp(request.name, "threshold") == 0) {
      if (!set_channel_threshold(ch, request.value, CONFIG_ORIGIN_WEB)) return false;
      canonical = ch ? right_thresh : left_thresh;
      return true;
    }
    return false;
  }

  if (strcmp(request.target, "shared") != 0) return false;
  if (strcmp(request.name, "a3_hz") == 0) {
    if (!set_a3_frequency(request.value, CONFIG_ORIGIN_WEB)) return false;
    canonical = a3_freq;
    return true;
  }
  if (strcmp(request.name, "tuning") == 0) {
    if (!set_tuning_index(lroundf(request.value), CONFIG_ORIGIN_WEB)) return false;
    canonical = use_alt_tuning ? alt_tuning_index : -1;
    return true;
  }
  if (strcmp(request.name, "pre_clip_gain") == 0) {
    if (!set_clip_gain(false, request.value, CONFIG_ORIGIN_WEB)) return false;
    canonical = pre_clip_gain;
    return true;
  }
  if (strcmp(request.name, "post_clip_gain") == 0) {
    if (!set_clip_gain(true, request.value, CONFIG_ORIGIN_WEB)) return false;
    canonical = post_clip_gain;
    return true;
  }
  const char *midiNames[3] = {"midi_left", "midi_right", "midi_both"};
  int *midiValues[3] = {&midi_ch_l, &midi_ch_r, &midi_ch_both};
  for (byte index = 0; index < 3; index++) {
    if (strcmp(request.name, midiNames[index]) != 0) continue;
    if (!set_midi_channel(index, lroundf(request.value), CONFIG_ORIGIN_WEB)) return false;
    canonical = *midiValues[index];
    return true;
  }
  return false;
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
    case wingie_serial::kOperationGetState:
      sendConfigurationState(request.id);
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
      float canonicalRatios[wingie_config::kRatioCount];
      for (uint8_t index = 0; index < wingie_config::kRatioCount; index++) {
        if (!wingie_config::canonicalizeRatio(request.ratios[index], canonicalRatios[index])) {
          sendError(request.id, "invalid_ratio", "ratios", nullptr);
          return;
        }
      }
      lock_config_state();
      if (request.hasExpectedRevision && request.expectedRevision != ratio_profile.revision) {
        unlock_config_state();
        sendError(request.id, "revision_conflict", "expected_revision", nullptr);
        return;
      }
      for (uint8_t index = 0; index < wingie_config::kRatioCount; index++) {
        ratio_profile.ratios[index] = canonicalRatios[index];
      }
      ratio_profile.revision++;
      ratio_profile.dirty = true;
      apply_ratio_profile_to_dsp();
      mark_config_state_changed(CONFIG_ORIGIN_WEB);
      const uint32_t resourceRevision = ratio_profile.revision;
      unlock_config_state();
      sendQueued(request.id, "set", resourceRevision);
      return;
    }
    case wingie_serial::kOperationSetRatioValue: {
      float canonical = 0.0f;
      lock_config_state();
      if (request.hasExpectedRevision && request.expectedRevision != ratio_profile.revision) {
        unlock_config_state();
        sendError(request.id, "revision_conflict", "expected_revision", nullptr);
        return;
      }
      if (!set_ratio_value(request.index, request.value, CONFIG_ORIGIN_WEB, &canonical)) {
        unlock_config_state();
        sendError(request.id, "invalid_ratio", "value", nullptr);
        return;
      }
      const uint32_t resourceRevision = ratio_profile.revision;
      unlock_config_state();
      sendValueResponse(request.id, "set_ratio_value", canonical,
                        fabsf(canonical - request.value) > 0.000001f, resourceRevision);
      return;
    }
    case wingie_serial::kOperationSetCave: {
      const byte ch = caveSideIndex(request.side);
      float canonicalFrequencies[wingie_config::kRatioCount];
      for (uint8_t index = 0; index < wingie_config::kRatioCount; index++) {
        if (!wingie_config::canonicalizeCaveFrequency(request.frequencies[index], canonicalFrequencies[index])) {
          sendError(request.id, "invalid_cave_bank", "frequencies", nullptr);
          return;
        }
      }
      lock_config_state();
      if (request.hasExpectedRevision && request.expectedRevision != cave_config_revision[ch][request.bank]) {
        unlock_config_state();
        sendError(request.id, "revision_conflict", "expected_revision", nullptr);
        return;
      }
      for (uint8_t index = 0; index < wingie_config::kRatioCount; index++) {
        cm_freq[ch][request.bank][index] = canonicalFrequencies[index];
        cm_ms[ch][request.bank][index] = request.mute[index];
      }
      mark_cave_changed(ch, request.bank, CONFIG_ORIGIN_WEB);
      applyCaveBank(ch, request.bank);
      const uint32_t resourceRevision = cave_config_revision[ch][request.bank];
      unlock_config_state();
      sendQueued(request.id, "set_cave", resourceRevision);
      return;
    }
    case wingie_serial::kOperationSetCaveValue: {
      const byte ch = caveSideIndex(request.target);
      float canonical = 0.0f;
      lock_config_state();
      if (request.hasExpectedRevision && request.expectedRevision != cave_config_revision[ch][request.bank]) {
        unlock_config_state();
        sendError(request.id, "revision_conflict", "expected_revision", nullptr);
        return;
      }
      if (!set_cave_frequency(ch, request.bank, request.index, request.value,
                              CONFIG_ORIGIN_WEB, &canonical)) {
        unlock_config_state();
        sendError(request.id, "invalid_cave_frequency", "value", nullptr);
        return;
      }
      const uint32_t resourceRevision = cave_config_revision[ch][request.bank];
      unlock_config_state();
      sendValueResponse(request.id, "set_cave_value", canonical,
                        fabsf(canonical - request.value) > 0.0001f,
                        resourceRevision);
      return;
    }
    case wingie_serial::kOperationSetCaveMute: {
      const byte ch = caveSideIndex(request.target);
      lock_config_state();
      if (request.hasExpectedRevision && request.expectedRevision != cave_config_revision[ch][request.bank]) {
        unlock_config_state();
        sendError(request.id, "revision_conflict", "expected_revision", nullptr);
        return;
      }
      if (!set_cave_mute(ch, request.bank, request.index, request.muteValue, CONFIG_ORIGIN_WEB)) {
        unlock_config_state();
        sendError(request.id, "invalid_cave_mute", "mute", nullptr);
        return;
      }
      const uint32_t resourceRevision = cave_config_revision[ch][request.bank];
      unlock_config_state();
      sendMuteResponse(request.id, request.muteValue, resourceRevision);
      return;
    }
    case wingie_serial::kOperationSetParam: {
      float canonical = 0.0f;
      lock_config_state();
      if (request.hasExpectedRevision && request.expectedRevision != config_state_revision) {
        unlock_config_state();
        sendError(request.id, "revision_conflict", "expected_revision", nullptr);
        return;
      }
      if (!applyScalarParameter(request, canonical)) {
        unlock_config_state();
        sendError(request.id, "invalid_parameter", request.name, nullptr);
        return;
      }
      const uint32_t resourceRevision = config_state_revision;
      unlock_config_state();
      sendValueResponse(request.id, "set_param", canonical,
                        fabsf(canonical - request.value) > 0.00001f, resourceRevision);
      return;
    }
    case wingie_serial::kOperationSave: {
      if (!save_serial_configuration()) {
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
      lock_config_state();
      if (request.hasExpectedRevision && request.expectedRevision != ratio_profile.revision) {
        unlock_config_state();
        sendError(request.id, "revision_conflict", "expected_revision", nullptr);
        return;
      }
      const uint32_t nextRevision = ratio_profile.revision + 1;
      wingie_config::setFactoryRatios(ratio_profile);
      ratio_profile.revision = nextRevision;
      ratio_profile.dirty = true;
      apply_ratio_profile_to_dsp();
      mark_config_state_changed(CONFIG_ORIGIN_WEB);
      const uint32_t resourceRevision = ratio_profile.revision;
      unlock_config_state();
      sendQueued(request.id, "reset", resourceRevision);
      return;
    }
    default:
      sendError(request.id, "unknown_operation", nullptr, nullptr);
      return;
  }
}

void servicePendingConfigEvent() {
  if (!serial_config_ready) return;
  const unsigned long now = millis();
  if (now - serialConfigEventTimer < 25) return;
  lock_config_state();
  if (!config_event_pending) {
    unlock_config_state();
    return;
  }
  const uint32_t revision = config_state_revision;
  const ConfigOrigin origin = config_last_origin;
  config_event_pending = false;
  unlock_config_state();
  serialConfigEventTimer = now;
  sendChangedEvent(revision, origin);
  lock_config_state();
  if (config_state_revision != revision) config_event_pending = true;
  unlock_config_state();
}

}  // namespace

bool load_cave_bank_from_preferences(Preferences &store, byte ch, byte bank, bool unquantized) {
  return loadCaveBankV2(store, ch, bank, unquantized);
}

void apply_ratio_profile_to_dsp() {
  for (byte ch = 0; ch < 2; ch++) {
    if (Mode[ch] == RATIO_MODE) {
      apply_pitched_mode_channel(ch, currentNote[ch]);
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

bool save_ratio_and_cave_preferences(Preferences &store, bool saveUnquantizedBanks,
                                     uint32_t tuningRevisionSnapshot,
                                     bool &currentCavesSaved, bool &unquantizedCavesSaved) {
  bool saved = true;
  wingie_config::RatioProfileState ratioSnapshot;
  lock_config_state();
  ratioSnapshot = ratio_profile;
  unlock_config_state();
  if (ratioSnapshot.dirty) {
    bool profileSaved = true;
    wingie_config::RatioProfileStorage storage;
    if (!wingie_config::encodeRatioProfile(ratioSnapshot, storage) ||
        store.putBytes(kRatioProfileKey, &storage, sizeof(storage)) != sizeof(storage)) {
      profileSaved = false;
    } else {
      wingie_config::RatioProfileStorage verified;
      wingie_config::RatioProfileState decoded;
      if (store.getBytes(kRatioProfileKey, &verified, sizeof(verified)) != sizeof(verified) ||
          !wingie_config::decodeRatioProfile(verified, decoded) ||
          decoded.revision != ratioSnapshot.revision) {
        profileSaved = false;
      } else {
        for (byte index = 0; index < wingie_config::kRatioCount; index++) {
          if (fabsf(decoded.ratios[index] - ratioSnapshot.ratios[index]) > 0.00051f) profileSaved = false;
        }
      }
    }
    if (profileSaved) {
      lock_config_state();
      if (ratio_profile.revision == ratioSnapshot.revision) ratio_profile.dirty = false;
      else profileSaved = false;
      unlock_config_state();
    }
    if (!profileSaved) saved = false;
  }

  currentCavesSaved = true;
  for (byte ch = 0; ch < 2; ch++) {
    for (byte bank = 0; bank < wingie_config::kCaveBankCount; bank++) {
      wingie_config::CaveBankState snapshot;
      lock_config_state();
      const bool shouldSave = cave_config_dirty[ch][bank] || cave_storage_migration_pending;
      memset(&snapshot, 0, sizeof(snapshot));
      for (byte voice = 0; voice < wingie_config::kRatioCount; voice++) {
        snapshot.frequencies[voice] = cm_freq[ch][bank][voice];
        snapshot.mute[voice] = cm_ms[ch][bank][voice];
      }
      snapshot.revision = cave_config_revision[ch][bank];
      snapshot.dirty = false;
      unlock_config_state();
      if (!shouldSave) continue;

      bool bankSaved = true;
      if (!saveCaveBankV2(store, ch, bank, false, snapshot)) {
        bankSaved = false;
      }
      for (byte voice = 0; voice < wingie_config::kRatioCount; voice++) {
        char key[16];
        snprintf(key, sizeof(key), ch ? "r_cf_%u_%u" : "l_cf_%u_%u", bank, voice);
        if (!store.putUShort(key, static_cast<uint16_t>(lroundf(snapshot.frequencies[voice])))) {
          bankSaved = false;
        }
        snprintf(key, sizeof(key), ch ? "r_cms_%u_%u" : "l_cms_%u_%u", bank, voice);
        if (!store.putBool(key, snapshot.mute[voice])) {
          bankSaved = false;
        }
      }
      if (bankSaved) {
        lock_config_state();
        if (cave_config_revision[ch][bank] == snapshot.revision) {
          cave_config_dirty[ch][bank] = false;
          for (byte voice = 0; voice < wingie_config::kRatioCount; voice++) {
            cm_freq_prev[ch][bank][voice] = snapshot.frequencies[voice];
            cm_ms_prev[ch][bank][voice] = snapshot.mute[voice];
          }
        } else {
          bankSaved = false;
        }
        unlock_config_state();
      } else {
        lock_config_state();
        cave_config_dirty[ch][bank] = true;
        unlock_config_state();
      }
      if (!bankSaved) {
        saved = false;
        currentCavesSaved = false;
      }
    }
  }

  unquantizedCavesSaved = true;
  if (saveUnquantizedBanks) {
    wingie_config::CaveBankState snapshots[2][wingie_config::kCaveBankCount];
    memset(snapshots, 0, sizeof(snapshots));
    lock_config_state();
    for (byte ch = 0; ch < 2; ch++) {
      for (byte bank = 0; bank < wingie_config::kCaveBankCount; bank++) {
        for (byte voice = 0; voice < wingie_config::kRatioCount; voice++) {
          snapshots[ch][bank].frequencies[voice] = cm_freq_stored_unq[ch][bank][voice];
        }
      }
    }
    unlock_config_state();
    for (byte ch = 0; ch < 2; ch++) {
      for (byte bank = 0; bank < wingie_config::kCaveBankCount; bank++) {
        const wingie_config::CaveBankState &snapshot = snapshots[ch][bank];
        if (!saveCaveBankV2(store, ch, bank, true, snapshot)) {
          saved = false;
          unquantizedCavesSaved = false;
        }
        for (byte voice = 0; voice < wingie_config::kRatioCount; voice++) {
          char key[16];
          snprintf(key, sizeof(key), ch ? "r_cf_unq_%u_%u" : "l_cf_unq_%u_%u", bank, voice);
          if (!store.putUShort(key, static_cast<uint16_t>(lroundf(snapshot.frequencies[voice])))) {
            saved = false;
            unquantizedCavesSaved = false;
          }
        }
      }
    }
    lock_config_state();
    if (tuning_revision == tuningRevisionSnapshot && unquantizedCavesSaved) {
      unquantized_caves_dirty = false;
    } else {
      unquantizedCavesSaved = false;
      saved = false;
    }
    unlock_config_state();
  }

  lock_config_state();
  if (cave_storage_migration_pending && currentCavesSaved && unquantizedCavesSaved) {
    cave_storage_migration_pending = false;
  }
  unlock_config_state();
  return saved;
}

bool save_serial_configuration() {
  const bool saved = save_all_configuration();
  if (saved) mark_config_state_changed(CONFIG_ORIGIN_WEB);
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
  servicePendingConfigEvent();
}
