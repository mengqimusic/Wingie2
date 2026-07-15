#ifndef WINGIE2_CONFIG_PROFILES_H
#define WINGIE2_CONFIG_PROFILES_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace wingie_config {

static const uint8_t kRatioCount = 9;
static const uint8_t kCaveBankCount = 3;
static const float kRatioMin = 0.125f;
static const float kRatioMax = 32.0f;
static const float kRatioStep = 0.001f;
static const uint16_t kRatioFrequencyMin = 16;
static const uint16_t kRatioFrequencyMax = 16000;
static const uint32_t kRatioScale = 1000;
static const uint16_t kCaveFrequencyMin = 8;
static const uint16_t kCaveFrequencyMax = 15999;
static const uint32_t kRatioProfileMagic = 0x31544152;
static const uint16_t kRatioProfileVersion = 1;

static const float kDefaultRatios[kRatioCount] = {
  1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f
};

struct RatioProfileState {
  float ratios[kRatioCount];
  uint32_t revision;
  bool dirty;
};

struct CaveBankState {
  uint16_t frequencies[kRatioCount];
  bool mute[kRatioCount];
  uint32_t revision;
  bool dirty;
};

struct RatioProfileStorage {
  uint32_t magic;
  uint16_t version;
  uint16_t scale;
  uint32_t revision;
  uint32_t ratiosMilli[kRatioCount];
  uint32_t crc;
};

static_assert(sizeof(RatioProfileStorage) == 52, "Ratio profile storage layout changed");

inline bool finite(float value) {
  return isfinite(value);
}

inline bool ratioToMilli(float value, uint32_t &ratioMilli) {
  if (!finite(value) || value < kRatioMin || value > kRatioMax) return false;
  const long rounded = lroundf(value * kRatioScale);
  if (rounded < lroundf(kRatioMin * kRatioScale) || rounded > lroundf(kRatioMax * kRatioScale)) return false;
  const float canonical = static_cast<float>(rounded) / kRatioScale;
  if (fabsf(value - canonical) > (kRatioStep * 0.51f)) return false;
  ratioMilli = static_cast<uint32_t>(rounded);
  return true;
}

inline bool validateRatios(const float *ratios, size_t count) {
  if (!ratios || count != kRatioCount) return false;
  for (size_t index = 0; index < count; index++) {
    uint32_t ignored = 0;
    if (!ratioToMilli(ratios[index], ignored)) return false;
  }
  return true;
}

inline bool validateCaveBank(const uint16_t *frequencies, const bool *mute, size_t count) {
  if (!frequencies || !mute || count != kRatioCount) return false;
  for (size_t index = 0; index < count; index++) {
    if (frequencies[index] < kCaveFrequencyMin || frequencies[index] > kCaveFrequencyMax) return false;
  }
  return true;
}

inline void setFactoryRatios(RatioProfileState &profile) {
  memcpy(profile.ratios, kDefaultRatios, sizeof(kDefaultRatios));
  profile.revision = 0;
  profile.dirty = false;
}

inline uint32_t crc32(const uint8_t *data, size_t length) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t index = 0; index < length; index++) {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8; bit++) {
      const uint32_t mask = -(crc & 1u);
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return ~crc;
}

inline bool encodeRatioProfile(const RatioProfileState &profile, RatioProfileStorage &storage) {
  if (!validateRatios(profile.ratios, kRatioCount)) return false;
  memset(&storage, 0, sizeof(storage));
  storage.magic = kRatioProfileMagic;
  storage.version = kRatioProfileVersion;
  storage.scale = kRatioScale;
  storage.revision = profile.revision;
  for (uint8_t index = 0; index < kRatioCount; index++) {
    if (!ratioToMilli(profile.ratios[index], storage.ratiosMilli[index])) return false;
  }
  storage.crc = crc32(reinterpret_cast<const uint8_t *>(&storage), offsetof(RatioProfileStorage, crc));
  return true;
}

inline bool decodeRatioProfile(const RatioProfileStorage &storage, RatioProfileState &profile) {
  if (storage.magic != kRatioProfileMagic || storage.version != kRatioProfileVersion || storage.scale != kRatioScale) return false;
  const uint32_t expected = crc32(reinterpret_cast<const uint8_t *>(&storage), offsetof(RatioProfileStorage, crc));
  if (storage.crc != expected) return false;
  for (uint8_t index = 0; index < kRatioCount; index++) {
    if (storage.ratiosMilli[index] < kRatioMin * kRatioScale || storage.ratiosMilli[index] > kRatioMax * kRatioScale) return false;
    profile.ratios[index] = static_cast<float>(storage.ratiosMilli[index]) / kRatioScale;
  }
  profile.revision = storage.revision;
  profile.dirty = false;
  return true;
}

}  // namespace wingie_config

#endif
