#ifndef WINGIE2_DECAY_EXPRESSION_H
#define WINGIE2_DECAY_EXPRESSION_H

#include <stdint.h>

// 把多声部表情增量折成一侧一根 decay：相加。深度分两档（调用方传入）：
// poly/ratio 每音 2 秒（一侧三音最多 6 秒）；string/bar 与常规整侧单音 6 秒。
// 写入 Faust 现有 decay 滑条；推杆本身 0.1–10，压力可越过推杆满位，
// 总和钳制在 20 秒上限内。不新增热路径参数。
// 全行程线性：Osmose 满压前发 0xD0(0-127)，满压后的剩余行程发同通道 CC74(0-127)，
// 两段拼成 0–254 的物理行程轴，boost = depth · E/254。
// 实测依据：自然演奏中 CC74>0 时 0xD0 恒为 127，两段交接无重叠。

namespace wingie_decay {

static const float kFaderMin = 0.1f;
static const float kFaderMax = 20.0f;
static const float kPressureDepthPerVoiceSeconds = 2.0f;
static const float kPressureDepthMonoSeconds = 6.0f;
static const float kPressureSumMax = 6.0f;
static const float kTravelMax = 254.0f;

inline float clamp(float value, float lo, float hi) {
  if (value < lo) return lo;
  if (value > hi) return hi;
  return value;
}

// E = pressure(0xD0) + timbre(CC74)，各 0-127，全行程线性。
inline float pressure_boost(uint8_t pressure, uint8_t timbre, float depth) {
  return depth * (pressure + timbre) / kTravelMax;
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
