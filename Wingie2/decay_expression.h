#ifndef WINGIE2_DECAY_EXPRESSION_H
#define WINGIE2_DECAY_EXPRESSION_H

#include <stdint.h>

// 把多声部 0xD0 衰减增量折成一侧一根 decay：相加，每音最多 2 秒，一侧最多 6 秒。
// 写入 Faust 现有 decay 滑条（0.1–20），不新增热路径参数。
// 0xD0 用 n=5 前慢后快曲线：round(127·(p/127)⁵)，满压仍是 +2 秒。

namespace wingie_decay {

static const float kFaderMin = 0.1f;
static const float kFaderMax = 20.0f;
static const float kPressureDepthSeconds = 2.0f;
static const float kPressureSumMax = 6.0f;
static const float kPressureMax = 127.0f;

inline float clamp(float value, float lo, float hi) {
  if (value < lo) return lo;
  if (value > hi) return hi;
  return value;
}

// 与整数 round(127·(p/127)⁵) 同式：p⁵ / 127⁴，127⁴ = 260144641。
inline uint8_t pressure_curve_to5(uint8_t pressure) {
  const uint64_t p2 = static_cast<uint64_t>(pressure) * pressure;
  const uint64_t p5 = p2 * p2 * static_cast<uint64_t>(pressure);
  constexpr uint64_t kDen = 260144641ull;
  return static_cast<uint8_t>((p5 + kDen / 2) / kDen);
}

inline float pressure_boost(uint8_t pressure, float depth) {
  return depth * static_cast<float>(pressure_curve_to5(pressure)) / kPressureMax;
}

inline float side_boost(const float *voices, int count) {
  float sum = 0.0f;
  for (int i = 0; i < count; i++) sum += voices[i];
  return clamp(sum, 0.0f, kPressureSumMax);
}

inline float effective_t60(float fader, float boost) {
  return clamp(fader + boost, kFaderMin, kFaderMax);
}

}  // namespace wingie_decay

#endif
