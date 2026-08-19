#include <assert.h>
#include <math.h>

#include "../../Wingie2/decay_expression.h"

static void assertNear(float actual, float expected) {
  assert(fabsf(actual - expected) < 1e-6f);
}

int main() {
  assert(wingie_decay::pressure_curve_to5(0) == 0);
  assert(wingie_decay::pressure_curve_to5(127) == 127);
  assert(wingie_decay::pressure_curve_to5(100) == 38);
  assert(wingie_decay::pressure_curve_to5(115) == 77);
  assert(wingie_decay::pressure_curve_to5(120) == 96);
  assert(wingie_decay::pressure_curve_to5(124) == 113);

  assertNear(wingie_decay::pressure_boost(0, 2.0f), 0.0f);
  assertNear(wingie_decay::pressure_boost(127, 2.0f), 2.0f);
  assertNear(wingie_decay::pressure_boost(100, 2.0f), 2.0f * 38.0f / 127.0f);

  const float none[3] = {0.0f, 0.0f, 0.0f};
  assertNear(wingie_decay::side_boost(none, 3), 0.0f);

  const float mixed[3] = {0.5f, 2.0f, 1.2f};
  assertNear(wingie_decay::side_boost(mixed, 3), 3.7f);

  const float all_full[3] = {2.0f, 2.0f, 2.0f};
  assertNear(wingie_decay::side_boost(all_full, 3), 6.0f);

  // 和超过单侧上限时钳制到 6 秒。
  const float over_sum[3] = {2.0f, 2.0f, 2.5f};
  assertNear(wingie_decay::side_boost(over_sum, 3), wingie_decay::kPressureSumMax);

  // 有效 t60 = fader + boost，clamp 到滑条范围 0.1–10。
  assertNear(wingie_decay::effective_t60(3.0f, 2.0f), 5.0f);
  assertNear(wingie_decay::effective_t60(5.0f, 6.0f), 10.0f);
  assertNear(wingie_decay::effective_t60(10.0f, 6.0f), 10.0f);
  assertNear(wingie_decay::effective_t60(0.1f, 0.0f), 0.1f);
  return 0;
}
