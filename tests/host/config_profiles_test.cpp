#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "../../Wingie2/config_profiles.h"

using namespace wingie_config;

static void testFactoryProfile() {
  assert(kRatioFrequencyMin == 16);
  assert(kRatioFrequencyMax == 16000);
  assert(fabsf(kCaveFrequencyMin - 16.0f) < 1e-6f);
  assert(fabsf(kCaveFrequencyMax - 16000.0f) < 1e-6f);
  assert(fabsf(kCaveFrequencyStep - 0.01f) < 1e-6f);
  assert(kCaveFrequencyScale == 100);
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

static void testCaveCanonicalization() {
  float canonical = 0.0f;
  assert(canonicalizeCaveFrequency(8.0f, canonical));
  assert(fabsf(canonical - 16.0f) < 1e-6f);
  assert(canonicalizeCaveFrequency(440.126f, canonical));
  assert(fabsf(canonical - 440.13f) < 1e-4f);
  assert(canonicalizeCaveFrequency(20000.0f, canonical));
  assert(fabsf(canonical - 16000.0f) < 1e-3f);
  assert(!canonicalizeCaveFrequency(INFINITY, canonical));
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
  float frequencies[kRatioCount] = {16.0f, 50.25f, 115.5f, 218.75f, 411.0f, 777.0f, 1500.0f, 5200.0f, 16000.0f};
  bool mute[kRatioCount] = {};
  assert(validateCaveBank(frequencies, mute, kRatioCount));
  frequencies[0] = 15.99f;
  assert(!validateCaveBank(frequencies, mute, kRatioCount));
  frequencies[0] = 16.0f;
  frequencies[8] = 16000.01f;
  assert(!validateCaveBank(frequencies, mute, kRatioCount));
  frequencies[8] = NAN;
  assert(!validateCaveBank(frequencies, mute, kRatioCount));
}

static void testCaveRoundTripAndLegacyMatch() {
  CaveBankState original = {};
  const float frequencies[kRatioCount] = {
    16.01f, 50.25f, 115.5f, 218.75f, 411.0f, 777.0f, 1500.0f, 5200.0f, 16000.0f
  };
  memcpy(original.frequencies, frequencies, sizeof(frequencies));
  original.mute[1] = true;
  original.mute[8] = true;
  original.revision = 27;
  original.dirty = true;

  CaveBankStorage storage;
  assert(encodeCaveBank(original, storage));
  assert(sizeof(storage) == 56);
  assert(storage.magic == 0x32564143);
  assert(storage.version == 2);
  assert(storage.scale == 100);
  assert(storage.frequenciesCentiHz[0] == 1601);
  assert(storage.frequenciesCentiHz[1] == 5025);
  assert(storage.frequenciesCentiHz[8] == 1600000);

  CaveBankState decoded;
  assert(decodeCaveBank(storage, decoded));
  assert(decoded.revision == 27);
  assert(!decoded.dirty);
  for (uint8_t index = 0; index < kRatioCount; index++) {
    assert(fabsf(decoded.frequencies[index] - frequencies[index]) < 1e-3f);
    assert(decoded.mute[index] == original.mute[index]);
  }

  uint16_t legacyFrequencies[kRatioCount];
  bool legacyMute[kRatioCount];
  for (uint8_t index = 0; index < kRatioCount; index++) {
    legacyFrequencies[index] = static_cast<uint16_t>(lroundf(decoded.frequencies[index]));
    legacyMute[index] = decoded.mute[index];
  }
  assert(caveBankMatchesLegacy(decoded, legacyFrequencies, legacyMute, kRatioCount));
  legacyFrequencies[3]++;
  assert(!caveBankMatchesLegacy(decoded, legacyFrequencies, legacyMute, kRatioCount));
  legacyFrequencies[3]--;
  legacyMute[8] = false;
  assert(!caveBankMatchesLegacy(decoded, legacyFrequencies, legacyMute, kRatioCount));
}

static void testCorruptedCaveStorageFailsAtomically() {
  CaveBankState bank = {};
  for (uint8_t index = 0; index < kRatioCount; index++) bank.frequencies[index] = 440.0f + index;
  CaveBankStorage storage;
  assert(encodeCaveBank(bank, storage));
  storage.frequenciesCentiHz[2]++;

  CaveBankState decoded = {};
  decoded.frequencies[0] = 123.45f;
  assert(!decodeCaveBank(storage, decoded));
  assert(fabsf(decoded.frequencies[0] - 123.45f) < 1e-4f);

  assert(encodeCaveBank(bank, storage));
  storage.muteBits |= static_cast<uint16_t>(1u << kRatioCount);
  storage.crc = crc32(reinterpret_cast<const uint8_t *>(&storage), offsetof(CaveBankStorage, crc));
  assert(!decodeCaveBank(storage, decoded));
  assert(fabsf(decoded.frequencies[0] - 123.45f) < 1e-4f);
}

int main() {
  testFactoryProfile();
  testRatioValidation();
  testCaveCanonicalization();
  testRatioRoundTrip();
  testCorruptedStorageFails();
  testCaveValidation();
  testCaveRoundTripAndLegacyMatch();
  testCorruptedCaveStorageFailsAtomically();
  return 0;
}
