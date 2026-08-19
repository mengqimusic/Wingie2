#include <assert.h>
#include <math.h>

#include "../../Wingie2/decay_expression.h"

static void assertNear(float actual, float expected) {
  assert(fabsf(actual - expected) < 1e-6f);
}

int main() {
  assertNear(wingie_decay::pressure_boost(0, 3.0f), 0.0f);
  assertNear(wingie_decay::pressure_boost(127, 3.0f), 3.0f);
  assertNear(wingie_decay::pressure_boost(63.5f, 3.0f), 1.5f);

  const float none[3] = {0.0f, 0.0f, 0.0f};
  assertNear(wingie_decay::side_boost(none, 3), 0.0f);

  const float mixed[3] = {0.5f, 3.0f, 1.2f};
  assertNear(wingie_decay::side_boost(mixed, 3), 3.0f);

  const float all_full[3] = {3.0f, 3.0f, 3.0f};
  assertNear(wingie_decay::side_boost(all_full, 3), 3.0f);

  assertNear(wingie_decay::effective_t60(5.0f, 3.0f), 8.0f);
  assertNear(wingie_decay::effective_t60(9.0f, 3.0f), 10.0f);
  assertNear(wingie_decay::effective_t60(0.1f, 0.0f), 0.1f);
  return 0;
}
