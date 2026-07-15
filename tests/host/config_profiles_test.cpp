#include <assert.h>
#include <math.h>
#include <stdint.h>

#include "../../Wingie2/config_profiles.h"

using namespace wingie_config;

static void testFactoryProfile() {
  assert(kRatioFrequencyMin == 16);
  assert(kRatioFrequencyMax == 16000);
  RatioProfileState profile;
  setFactoryRatios(profile);
  assert(profile.revision == 0);
  assert(!profile.dirty);
  for (uint8_t index = 0; index < kRatioCount; index++) {
    assert(profile.ratios[index] == kDefaultRatios[index]);
  }
}

static void testRatioValidation() {
  float ratios[kRatioCount] = {0.125f, 0.5f, 1.0f, 1.001f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f};
  assert(validateRatios(ratios, kRatioCount));
  ratios[0] = 0.124f;
  assert(!validateRatios(ratios, kRatioCount));
  ratios[0] = NAN;
  assert(!validateRatios(ratios, kRatioCount));
  ratios[0] = 0.125f;
  ratios[1] = 0.5006f;
  assert(validateRatios(ratios, kRatioCount));
  uint32_t ratioMilli = 0;
  assert(ratioToMilli(ratios[1], ratioMilli));
  assert(ratioMilli == 501);
}

static void testRatioRoundTrip() {
  RatioProfileState original;
  setFactoryRatios(original);
  original.ratios[0] = 0.125f;
  original.ratios[1] = 0.999f;
  original.ratios[8] = 31.999f;
  original.revision = 12;
  original.dirty = true;

  RatioProfileStorage storage;
  assert(encodeRatioProfile(original, storage));

  RatioProfileState decoded;
  assert(decodeRatioProfile(storage, decoded));
  assert(decoded.revision == 12);
  assert(!decoded.dirty);
  assert(fabsf(decoded.ratios[0] - 0.125f) < 1e-6f);
  assert(fabsf(decoded.ratios[1] - 0.999f) < 1e-6f);
  assert(fabsf(decoded.ratios[8] - 31.999f) < 1e-6f);
}

static void testCorruptedStorageFails() {
  RatioProfileState profile;
  setFactoryRatios(profile);
  RatioProfileStorage storage;
  assert(encodeRatioProfile(profile, storage));
  storage.ratiosMilli[2] += 1;

  RatioProfileState decoded;
  assert(!decodeRatioProfile(storage, decoded));
}

static void testCaveValidation() {
  uint16_t frequencies[kRatioCount] = {8, 50, 115, 218, 411, 777, 1500, 5200, 15999};
  bool mute[kRatioCount] = {};
  assert(validateCaveBank(frequencies, mute, kRatioCount));
  frequencies[0] = 7;
  assert(!validateCaveBank(frequencies, mute, kRatioCount));
  frequencies[0] = 8;
  frequencies[8] = 16000;
  assert(!validateCaveBank(frequencies, mute, kRatioCount));
}

int main() {
  testFactoryProfile();
  testRatioValidation();
  testRatioRoundTrip();
  testCorruptedStorageFails();
  testCaveValidation();
  return 0;
}
