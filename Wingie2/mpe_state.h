#ifndef WINGIE2_MPE_STATE_H
#define WINGIE2_MPE_STATE_H

#include <math.h>
#include <stdint.h>
#include <string.h>

namespace wingie_mpe {

static const uint8_t kChannelCount = 16;
static const uint8_t kSideCount = 2;
static const uint8_t kVoiceCount = 3;
static const uint8_t kLowerZone = 0;
static const uint8_t kUpperZone = 1;
static const int8_t kNoZone = -1;
static const uint8_t kStartupMemberCount = 6;
static const int16_t kPitchBendMinimum = -8192;
static const int16_t kPitchBendMaximum = 8191;

struct PitchBendRange {
  uint8_t semitones;
  uint8_t cents;
};

struct ChannelState {
  uint8_t rpnMsb;
  uint8_t rpnLsb;
  int16_t pitchBend;
  PitchBendRange conventionalRange;
};

struct ZoneState {
  uint8_t managerChannel;
  uint16_t memberMask;
  PitchBendRange managerRange;
  PitchBendRange memberRange;
};

struct VoiceState {
  bool active;
  uint8_t channel;
  uint8_t note;
  uint32_t allocatedAt;
  float memberBendSemitones;
};

inline uint16_t channelBit(uint8_t channel) {
  if (channel < 1 || channel > kChannelCount) return 0;
  return static_cast<uint16_t>(1u << (channel - 1));
}

inline float rangeSemitones(const PitchBendRange &range) {
  return range.semitones + range.cents / 100.0f;
}

inline float normalizedPitchBend(int16_t pitchBend) {
  if (pitchBend >= 0) return pitchBend / static_cast<float>(kPitchBendMaximum);
  return pitchBend / static_cast<float>(-kPitchBendMinimum);
}

inline float pitchBendSemitones(int16_t pitchBend, const PitchBendRange &range) {
  return normalizedPitchBend(pitchBend) * rangeSemitones(range);
}

inline float pitchRatio(float semitones) {
  return powf(2.0f, semitones / 12.0f);
}

inline float totalPitchBend(bool mpeOwned, float conventional, float manager, float member) {
  return mpeOwned ? manager + member : conventional;
}

struct State {
  ZoneState zones[2];
  ChannelState channels[kChannelCount];
  VoiceState voices[kSideCount][kVoiceCount];
  uint32_t allocationCounter;

  void reset() {
    memset(this, 0, sizeof(*this));
    zones[kLowerZone].managerChannel = 1;
    zones[kUpperZone].managerChannel = 16;
    for (uint8_t zone = 0; zone < 2; zone++) {
      zones[zone].managerRange.semitones = 2;
      zones[zone].memberRange.semitones = 48;
    }
    for (uint8_t index = 0; index < kChannelCount; index++) {
      channels[index].rpnMsb = 127;
      channels[index].rpnLsb = 127;
      channels[index].conventionalRange.semitones = 2;
    }
  }

  bool zoneIsActive(uint8_t zone) const {
    return zone < 2 && zones[zone].memberMask != 0;
  }

  uint16_t claimedChannels() const {
    uint16_t claimed = 0;
    for (uint8_t zone = 0; zone < 2; zone++) {
      if (!zoneIsActive(zone)) continue;
      claimed |= zones[zone].memberMask;
      claimed |= channelBit(zones[zone].managerChannel);
    }
    return claimed;
  }

  int8_t zoneForChannel(uint8_t channel) const {
    const uint16_t bit = channelBit(channel);
    if (!bit) return kNoZone;
    for (uint8_t zone = 0; zone < 2; zone++) {
      if (!zoneIsActive(zone)) continue;
      if (channel == zones[zone].managerChannel || (zones[zone].memberMask & bit)) return zone;
    }
    return kNoZone;
  }

  bool channelIsManager(uint8_t channel) const {
    const int8_t zone = zoneForChannel(channel);
    return zone != kNoZone && zones[zone].managerChannel == channel;
  }

  uint16_t requestedMemberMask(uint8_t zone, uint8_t memberCount) const {
    if (zone > kUpperZone || memberCount == 0) return 0;
    if (memberCount > 15) memberCount = 15;
    uint16_t mask = 0;
    if (zone == kLowerZone) {
      for (uint8_t offset = 0; offset < memberCount; offset++) mask |= channelBit(2 + offset);
    } else {
      for (uint8_t offset = 0; offset < memberCount; offset++) mask |= channelBit(15 - offset);
    }
    return mask;
  }

  uint16_t configureZone(uint8_t zone, uint8_t memberCount) {
    if (zone > kUpperZone) return 0;
    int8_t beforeZone[kChannelCount];
    bool beforeManager[kChannelCount];
    for (uint8_t channel = 1; channel <= kChannelCount; channel++) {
      beforeZone[channel - 1] = zoneForChannel(channel);
      beforeManager[channel - 1] = channelIsManager(channel);
    }
    const uint8_t otherZone = zone == kLowerZone ? kUpperZone : kLowerZone;
    const uint16_t requested = requestedMemberMask(zone, memberCount);
    zones[zone].memberMask = 0;
    if (requested) {
      if (requested & channelBit(zones[otherZone].managerChannel)) zones[otherZone].memberMask = 0;
      zones[otherZone].memberMask &= ~requested;
      zones[otherZone].memberMask &= ~channelBit(zones[zone].managerChannel);
      zones[zone].memberMask = requested;
      zones[zone].managerRange = {2, 0};
      zones[zone].memberRange = {48, 0};
    }
    uint16_t changed = 0;
    for (uint8_t channel = 1; channel <= kChannelCount; channel++) {
      if (beforeZone[channel - 1] != zoneForChannel(channel) ||
          beforeManager[channel - 1] != channelIsManager(channel)) changed |= channelBit(channel);
    }
    return changed;
  }

  void selectRpn(uint8_t channel, uint8_t controller, uint8_t value) {
    if (channel < 1 || channel > kChannelCount) return;
    ChannelState &state = channels[channel - 1];
    if (controller == 101) state.rpnMsb = value;
    if (controller == 100) state.rpnLsb = value;
  }

  bool selectedRpnIs(uint8_t channel, uint8_t msb, uint8_t lsb) const {
    if (channel < 1 || channel > kChannelCount) return false;
    const ChannelState &state = channels[channel - 1];
    return state.rpnMsb == msb && state.rpnLsb == lsb;
  }

  void setPitchBendRange(uint8_t channel, uint8_t semitones, uint8_t cents) {
    if (channel < 1 || channel > kChannelCount) return;
    if (semitones > 96) semitones = 96;
    if (cents > 99) cents = 99;
    const PitchBendRange range = {semitones, cents};
    const int8_t zone = zoneForChannel(channel);
    if (zone == kNoZone) {
      channels[channel - 1].conventionalRange = range;
    } else if (zones[zone].managerChannel == channel) {
      zones[zone].managerRange = range;
    } else {
      zones[zone].memberRange = range;
    }
  }

  PitchBendRange pitchBendRange(uint8_t channel) const {
    const PitchBendRange fallback = {2, 0};
    if (channel < 1 || channel > kChannelCount) return fallback;
    const int8_t zone = zoneForChannel(channel);
    if (zone == kNoZone) return channels[channel - 1].conventionalRange;
    if (zones[zone].managerChannel == channel) return zones[zone].managerRange;
    return zones[zone].memberRange;
  }

  void setPitchBend(uint8_t channel, int pitchBend) {
    if (channel < 1 || channel > kChannelCount) return;
    if (pitchBend < kPitchBendMinimum) pitchBend = kPitchBendMinimum;
    if (pitchBend > kPitchBendMaximum) pitchBend = kPitchBendMaximum;
    channels[channel - 1].pitchBend = static_cast<int16_t>(pitchBend);
  }

  float channelPitchBendSemitones(uint8_t channel) const {
    if (channel < 1 || channel > kChannelCount) return 0.0f;
    return pitchBendSemitones(channels[channel - 1].pitchBend, pitchBendRange(channel));
  }

  float managerPitchBendSemitones(uint8_t zone) const {
    if (!zoneIsActive(zone)) return 0.0f;
    return channelPitchBendSemitones(zones[zone].managerChannel);
  }

  float memberPitchBendSemitones(uint8_t channel) const {
    const int8_t zone = zoneForChannel(channel);
    if (zone == kNoZone || zones[zone].managerChannel == channel) return 0.0f;
    return channelPitchBendSemitones(channel);
  }

  int allocateVoice(uint8_t side, uint8_t channel, uint8_t note) {
    if (side >= kSideCount) return -1;
    int selected = -1;
    uint32_t oldestAllocation = UINT32_MAX;
    for (uint8_t voice = 0; voice < kVoiceCount; voice++) {
      const VoiceState &candidate = voices[side][voice];
      if (!candidate.active) {
        selected = voice;
        break;
      }
      if (candidate.allocatedAt < oldestAllocation) {
        oldestAllocation = candidate.allocatedAt;
        selected = voice;
      }
    }
    VoiceState &voice = voices[side][selected];
    voice.active = true;
    voice.channel = channel;
    voice.note = note;
    voice.allocatedAt = ++allocationCounter;
    voice.memberBendSemitones = memberPitchBendSemitones(channel);
    return selected;
  }

  int releaseVoice(uint8_t side, uint8_t channel, uint8_t note) {
    if (side >= kSideCount) return -1;
    int selected = -1;
    uint32_t oldestAllocation = UINT32_MAX;
    for (uint8_t voice = 0; voice < kVoiceCount; voice++) {
      const VoiceState &candidate = voices[side][voice];
      if (!candidate.active || candidate.channel != channel || candidate.note != note) continue;
      if (candidate.allocatedAt < oldestAllocation) {
        oldestAllocation = candidate.allocatedAt;
        selected = voice;
      }
    }
    if (selected >= 0) voices[side][selected].active = false;
    return selected;
  }

  void clearVoiceOwnership(uint8_t side) {
    if (side >= kSideCount) return;
    for (uint8_t voice = 0; voice < kVoiceCount; voice++) {
      voices[side][voice].active = false;
      voices[side][voice].channel = 0;
      voices[side][voice].memberBendSemitones = 0.0f;
    }
  }
};

}

#endif
