#ifndef WINGIE2_DECAY_EXPRESSION_H
#define WINGIE2_DECAY_EXPRESSION_H

// 把多声部 0xD0 衰减增量折成一侧一根 decay：取最大，不累加。
// 写入 Faust 现有 decay 滑条（0.1–10），不新增热路径参数。

namespace wingie_decay {

static const float kFaderMin = 0.1f;
static const float kFaderMax = 10.0f;
static const float kPressureDepthSeconds = 3.0f;
static const float kPressureMax = 127.0f;

inline float clamp(float value, float lo, float hi) {
  if (value < lo) return lo;
  if (value > hi) return hi;
  return value;
}

inline float pressure_boost(float pressure, float depth) {
  return depth * pressure / kPressureMax;
}

inline float side_boost(const float *voices, int count) {
  float peak = 0.0f;
  for (int i = 0; i < count; i++) {
    if (voices[i] > peak) peak = voices[i];
  }
  return peak;
}

inline float effective_t60(float fader, float boost) {
  return clamp(fader + boost, kFaderMin, kFaderMax);
}

}  // namespace wingie_decay

#endif
