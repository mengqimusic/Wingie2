#ifndef WINGIE2_SERIAL_RESPONSE_H
#define WINGIE2_SERIAL_RESPONSE_H

// 串口配置协议响应编码层（Arduino-free 纯头）。
//
// 与 serial_config_protocol.h 的请求解析相对：本头只把参数化状态编码为协议
// JSON 响应帧。所有 encode* 函数不触碰任何全局状态 / Preferences / DSP 句柄，
// 由 serial_config.ino 读取全局状态（含 device_state.h 快照助手）后填充结构体
// 再调用，保证响应字节与设备状态解耦、可在 host 侧单测。

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config_profiles.h"
#include "serial_config_protocol.h"

namespace wingie_serial {

class JsonResponse {
public:
  char data[kMaxFrameBytes + 1];
  size_t length;
  bool valid;

  JsonResponse() : length(0), valid(true) {
    data[0] = '\0';
  }

  void append(const char *format, ...) __attribute__((format(printf, 2, 3))) {
    if (!valid || length >= kMaxFrameBytes) return;
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

// get_settings 单声道参数快照（serial_config.ino 读取 dsp/Mode/threshold 后填充）
struct ChannelSettings {
  int mode;
  float mix;
  float decay;
  float volume;
  float threshold;
};

// get_settings shared 段参数快照
struct SharedSettings {
  float a3Hz;
  int tuning;            // use_alt_tuning ? alt_tuning_index : -1
  float preClipGain;
  float postClipGain;
  int midiLeft;
  int midiRight;
  int midiBoth;
  bool mpeEnabled;
};

// status 响应快照
struct StatusSnapshot {
  int mode[2];
  int note[2];
  float fundamentalHz[2];
  uint32_t midiRx;
  uint32_t profileRevision;
  uint8_t activeBank[2];
  uint32_t caveRevision[2][wingie_config::kCaveBankCount];
};

// get_controls 响应快照（controlActivity 与 midi_rx_count 的整块拷贝）
struct ControlCountsSnapshot {
  uint32_t midiRx;
  int note[2];
  uint16_t key[2][12];
  uint16_t modeButton[2];
  uint16_t octButton[2][2];
  uint16_t sourceSwitch;
  uint16_t pot[3];
};

inline void appendRatioArray(JsonResponse &response, const float *ratios) {
  response.append("[");
  for (uint8_t index = 0; index < wingie_config::kRatioCount; index++) {
    response.append(index ? ",%.3f" : "%.3f", ratios[index]);
  }
  response.append("]");
}

inline void appendCaveFrequencyArray(JsonResponse &response, const float *frequencies) {
  response.append("[");
  for (uint8_t index = 0; index < wingie_config::kRatioCount; index++) {
    response.append(index ? ",%.2f" : "%.2f", frequencies[index]);
  }
  response.append("]");
}

inline void appendCaveMuteArray(JsonResponse &response, const bool *mute) {
  response.append("[");
  for (uint8_t index = 0; index < wingie_config::kRatioCount; index++) {
    response.append(index ? ",%s" : "%s", mute[index] ? "true" : "false");
  }
  response.append("]");
}

inline void appendControlCounterArray(JsonResponse &response, const volatile uint16_t *values,
                                      size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (i) response.append(",");
    response.append("%u", static_cast<unsigned>(values[i]));
  }
}

inline void encodeError(JsonResponse &response, uint32_t id, const char *code,
                        const char *field, const char *message) {
  response.append("{\"v\":1,\"id\":%lu,\"ok\":false,\"error\":{\"code\":\"%s\"",
                  static_cast<unsigned long>(id), code);
  if (field) response.append(",\"field\":\"%s\"", field);
  if (message) response.append(",\"message\":\"%s\"", message);
  response.append("}}");
}

inline void appendChannelSettings(JsonResponse &response, const ChannelSettings &settings) {
  response.append("{\"mode\":%d,\"mix\":%.4f,\"decay\":%.4f,\"volume\":%.4f,\"threshold\":%.4f}",
                  settings.mode, settings.mix, settings.decay, settings.volume, settings.threshold);
}

inline void encodeHello(JsonResponse &response, uint32_t id, const char *firmware) {
  response.append("{\"v\":1,\"id\":%lu,\"ok\":true,\"op\":\"hello\","
                  "\"device\":\"Wingie2\","
                  "\"firmware\":\"%s\","
                  "\"capabilities\":[\"settings\",\"ratio_mode\",\"cave_config\",\"mpe\"],"
                  "\"config_schema\":5,\"transport\":{\"baud\":115200,\"max_frame\":%u}}",
                  static_cast<unsigned long>(id), firmware,
                  static_cast<unsigned>(kMaxFrameBytes));
}

inline void encodeSettings(JsonResponse &response, uint32_t id, bool sourceLine, bool dirty,
                           const ChannelSettings &left, const ChannelSettings &right,
                           const SharedSettings &shared) {
  response.append("{\"v\":1,\"id\":%lu,\"ok\":true,\"op\":\"get_settings\","
                  "\"source\":\"%s\",\"dirty\":%s,\"left\":",
                  static_cast<unsigned long>(id), sourceLine ? "line" : "mic",
                  dirty ? "true" : "false");
  appendChannelSettings(response, left);
  response.append(",\"right\":");
  appendChannelSettings(response, right);
  response.append(",\"shared\":{\"a3_hz\":%.2f,\"tuning\":%d,"
                  "\"pre_clip_gain\":%.4f,\"post_clip_gain\":%.4f,"
                  "\"midi\":{\"left\":%d,\"right\":%d,\"both\":%d},\"mpe_enabled\":%s}}",
                  shared.a3Hz, shared.tuning,
                  shared.preClipGain, shared.postClipGain,
                  shared.midiLeft, shared.midiRight, shared.midiBoth,
                  shared.mpeEnabled ? "true" : "false");
}

inline void encodeRatioProfile(JsonResponse &response, uint32_t id, const float *ratios,
                               uint32_t revision, bool dirty) {
  response.append("{\"v\":1,\"id\":%lu,\"ok\":true,\"op\":\"get\",\"profile\":{\"ratios\":",
                  static_cast<unsigned long>(id));
  appendRatioArray(response, ratios);
  response.append(",\"revision\":%lu,\"dirty\":%s},\"factory_profile\":{\"ratios\":",
                  static_cast<unsigned long>(revision), dirty ? "true" : "false");
  appendRatioArray(response, wingie_config::kDefaultRatios);
  response.append("},\"limits\":{\"min\":%.3f,\"max\":%.3f,\"step\":%.3f,"
                  "\"frequency_min\":%u,\"frequency_max\":%u}}",
                  wingie_config::kRatioMin, wingie_config::kRatioMax, wingie_config::kRatioStep,
                  wingie_config::kRatioFrequencyMin, wingie_config::kRatioFrequencyMax);
}

inline void encodeCaveBank(JsonResponse &response, uint32_t id, const char *side, uint8_t bank,
                           bool active, const float *frequencies, const bool *mutes,
                           uint32_t revision, bool dirty) {
  response.append("{\"v\":1,\"id\":%lu,\"ok\":true,\"op\":\"get_cave\","
                  "\"side\":\"%s\",\"bank\":%u,\"active\":%s,\"frequencies\":",
                  static_cast<unsigned long>(id), side, static_cast<unsigned>(bank),
                  active ? "true" : "false");
  appendCaveFrequencyArray(response, frequencies);
  response.append(",\"mute\":");
  appendCaveMuteArray(response, mutes);
  response.append(",\"revision\":%lu,\"dirty\":%s,\"limits\":{\"min\":%.2f,\"max\":%.2f,\"step\":%.2f}}",
                  static_cast<unsigned long>(revision), dirty ? "true" : "false",
                  wingie_config::kCaveFrequencyMin, wingie_config::kCaveFrequencyMax,
                  wingie_config::kCaveFrequencyStep);
}

inline void encodeStatus(JsonResponse &response, uint32_t id, const StatusSnapshot &snapshot) {
  response.append("{\"v\":1,\"id\":%lu,\"ok\":true,\"op\":\"status\","
                  "\"mode\":{\"left\":%d,\"right\":%d},"
                  "\"note\":{\"left\":%d,\"right\":%d},"
                  "\"fundamental_hz\":{\"left\":%.3f,\"right\":%.3f},"
                  "\"midi_rx\":%lu,"
                  "\"profile_revision\":%lu,\"cave_active_bank\":{\"left\":%u,\"right\":%u},"
                  "\"cave_revision\":{\"left\":[%lu,%lu,%lu],\"right\":[%lu,%lu,%lu]}}",
                  static_cast<unsigned long>(id), snapshot.mode[0], snapshot.mode[1],
                  snapshot.note[0], snapshot.note[1],
                  snapshot.fundamentalHz[0], snapshot.fundamentalHz[1],
                  static_cast<unsigned long>(snapshot.midiRx),
                  static_cast<unsigned long>(snapshot.profileRevision),
                  static_cast<unsigned>(snapshot.activeBank[0]),
                  static_cast<unsigned>(snapshot.activeBank[1]),
                  static_cast<unsigned long>(snapshot.caveRevision[0][0]),
                  static_cast<unsigned long>(snapshot.caveRevision[0][1]),
                  static_cast<unsigned long>(snapshot.caveRevision[0][2]),
                  static_cast<unsigned long>(snapshot.caveRevision[1][0]),
                  static_cast<unsigned long>(snapshot.caveRevision[1][1]),
                  static_cast<unsigned long>(snapshot.caveRevision[1][2]));
}

inline void encodeControlCounts(JsonResponse &response, uint32_t id,
                                const ControlCountsSnapshot &snapshot) {
  response.append("{\"v\":1,\"id\":%lu,\"ok\":true,\"op\":\"get_controls\","
                  "\"midi_rx\":%lu,\"note\":{\"left\":%d,\"right\":%d},"
                  "\"counts\":{\"key\":{\"left\":[",
                  static_cast<unsigned long>(id), static_cast<unsigned long>(snapshot.midiRx),
                  snapshot.note[0], snapshot.note[1]);
  appendControlCounterArray(response, snapshot.key[0], 12);
  response.append("],\"right\":[");
  appendControlCounterArray(response, snapshot.key[1], 12);
  response.append("]},\"mode_button\":[");
  appendControlCounterArray(response, snapshot.modeButton, 2);
  response.append("],\"oct_button\":{\"left\":[");
  appendControlCounterArray(response, snapshot.octButton[0], 2);
  response.append("],\"right\":[");
  appendControlCounterArray(response, snapshot.octButton[1], 2);
  response.append("]},\"source_switch\":%u,\"pot\":[",
                  static_cast<unsigned>(snapshot.sourceSwitch));
  appendControlCounterArray(response, snapshot.pot, 3);
  response.append("]}}");
}

inline void encodeQueued(JsonResponse &response, uint32_t id, const char *operation,
                         uint32_t revision) {
  response.append("{\"v\":1,\"id\":%lu,\"ok\":true,\"op\":\"%s\",\"state\":\"queued\",\"revision\":%lu}",
                  static_cast<unsigned long>(id), operation,
                  static_cast<unsigned long>(revision));
}

inline void encodeParameterResponse(JsonResponse &response, uint32_t id, float value, bool dirty,
                                    bool cavesChanged) {
  response.append("{\"v\":1,\"id\":%lu,\"ok\":true,\"op\":\"set_param\","
                  "\"value\":%.4f,\"dirty\":%s,\"caves_changed\":%s}",
                  static_cast<unsigned long>(id), value,
                  dirty ? "true" : "false", cavesChanged ? "true" : "false");
}

inline void encodeSaveOk(JsonResponse &response, uint32_t id) {
  response.append("{\"v\":1,\"id\":%lu,\"ok\":true,\"op\":\"save\",\"state\":\"saved\"}",
                  static_cast<unsigned long>(id));
}

// 响应溢出 fallback 帧：encode* 后 valid==false 时写入。返回写入长度。
inline size_t encodeResponseTooLarge(char *out, size_t capacity) {
  const char *fallback = "{\"v\":1,\"id\":0,\"ok\":false,\"error\":{\"code\":\"response_too_large\"}}";
  const int written = snprintf(out, capacity, "%s", fallback);
  return written < 0 ? 0 : static_cast<size_t>(written);
}

}  // namespace wingie_serial

#endif  // WINGIE2_SERIAL_RESPONSE_H
