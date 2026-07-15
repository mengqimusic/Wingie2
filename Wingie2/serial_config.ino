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

void sendHello(uint32_t id) {
  JsonResponse response;
  response.append("{\"v\":1,\"id\":%lu,\"ok\":true,\"op\":\"hello\","
                  "\"device\":\"Wingie2\",\"capabilities\":[\"ratio_mode\",\"cave_config\"],"
                  "\"config_schema\":1,\"transport\":{\"baud\":115200,\"max_frame\":%u}}",
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
  JsonResponse response;
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
      if (request.hasExpectedRevision && request.expectedRevision != cave_config_revision[ch][request.bank]) {
        sendError(request.id, "revision_conflict", "expected_revision", nullptr);
        return;
      }
      if (!wingie_config::validateCaveBank(request.frequencies, request.mute, wingie_config::kRatioCount)) {
        sendError(request.id, "invalid_cave_bank", "frequencies", nullptr);
        return;
      }
      for (uint8_t index = 0; index < wingie_config::kRatioCount; index++) {
        uint32_t frequencyCentiHz = 0;
        wingie_config::caveFrequencyToCentiHz(request.frequencies[index], frequencyCentiHz);
        cm_freq[ch][request.bank][index] =
            static_cast<float>(frequencyCentiHz) / wingie_config::kCaveFrequencyScale;
        cm_ms[ch][request.bank][index] = request.mute[index];
      }
      mark_cave_changed(ch, request.bank);
      applyCaveBank(ch, request.bank);
      sendQueued(request.id, "set_cave", cave_config_revision[ch][request.bank]);
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
      return;
    }
    case wingie_serial::kOperationReset: {
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

bool save_unquantized_cave_preferences(Preferences &store) {
  bool saved = true;
  for (byte ch = 0; ch < 2; ch++) {
    for (byte bank = 0; bank < wingie_config::kCaveBankCount; bank++) {
      const bool shouldSave = unquantized_cave_config_dirty[ch][bank] ||
                              unquantized_cave_storage_migration_pending[ch][bank];
      if (!shouldSave) continue;

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
      if (bankSaved && unchanged) {
        unquantized_cave_config_dirty[ch][bank] = false;
        unquantized_cave_storage_migration_pending[ch][bank] = false;
      } else {
        saved = false;
      }
    }
  }
  return saved;
}

bool save_ratio_and_cave_preferences(Preferences &store) {
  bool saved = true;
  if (ratio_profile.dirty) {
    wingie_config::RatioProfileStorage storage;
    if (!wingie_config::encodeRatioProfile(ratio_profile, storage) ||
        store.putBytes(kRatioProfileKey, &storage, sizeof(storage)) != sizeof(storage)) {
      saved = false;
    } else {
      ratio_profile.dirty = false;
    }
  }

  for (byte ch = 0; ch < 2; ch++) {
    for (byte bank = 0; bank < wingie_config::kCaveBankCount; bank++) {
      const bool shouldSave = cave_config_dirty[ch][bank] || cave_storage_migration_pending[ch][bank];
      if (!shouldSave) continue;

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
        cave_config_dirty[ch][bank] = false;
        cave_storage_migration_pending[ch][bank] = false;
      } else {
        saved = false;
      }
    }
  }

  return saved;
}

bool save_serial_configuration() {
  prefs.begin("settings", RW_MODE);
  bool saved = save_ratio_and_cave_preferences(prefs);
  const bool unquantizedSaved = !unq_caves_store || save_unquantized_cave_preferences(prefs);
  if (!unquantizedSaved || !prefs.putBool("unq_caves_store", unq_caves_store)) saved = false;
  prefs.end();
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
