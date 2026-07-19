#include <assert.h>
#include <math.h>

#include "../../Wingie2/mpe_state.h"

using namespace wingie_mpe;

void testFullZoneClaimsAllChannels() {
  State state;
  state.reset();
  assert(state.claimedChannels() == 0);
  state.configureZone(kLowerZone, kFullZoneMemberCount);
  assert(state.claimedChannels() == 0xFFFF);
  for (uint8_t channel = 1; channel <= 16; channel++) {
    assert(state.zoneForChannel(channel) == kLowerZone);
  }
  assert(state.channelIsManager(1));
  assert(!state.channelIsManager(2));
  assert(!state.channelIsManager(16));
  assert(state.pitchBendRange(1).semitones == 2);
  assert(state.pitchBendRange(2).semitones == 48);
  assert(state.pitchBendRange(16).semitones == 48);
}

void testEmptyZoneClaimsNothing() {
  State state;
  state.reset();
  state.configureZone(kLowerZone, kFullZoneMemberCount);
  assert(state.claimedChannels() == 0xFFFF);
  state.configureZone(kLowerZone, 0);
  assert(state.claimedChannels() == 0);
  assert(state.zoneForChannel(1) == kNoZone);
  assert(state.zoneForChannel(8) == kNoZone);
  assert(state.zoneForChannel(16) == kNoZone);
}

void testRecentZoneConfigurationWins() {
  State state;
  state.reset();
  assert(state.configureZone(kLowerZone, 7) == 0x00FF);
  const uint16_t changed = state.configureZone(kUpperZone, 11);
  assert(changed & channelBit(5));
  assert(changed & channelBit(16));
  assert(!(changed & channelBit(2)));
  assert(state.zoneForChannel(2) == kLowerZone);
  assert(state.zoneForChannel(4) == kLowerZone);
  assert(state.zoneForChannel(5) == kUpperZone);
  assert(state.zoneForChannel(15) == kUpperZone);
  state.configureZone(kLowerZone, 15);
  assert(!state.zoneIsActive(kUpperZone));
  assert(state.zoneForChannel(16) == kLowerZone);
}

void testPitchBendRangesAndEndpoints() {
  State state;
  state.reset();
  state.configureZone(kLowerZone, 6);
  state.setPitchBend(2, kPitchBendMaximum);
  assert(fabsf(state.channelPitchBendSemitones(2) - 48.0f) < 0.0001f);
  state.setPitchBend(2, kPitchBendMinimum);
  assert(fabsf(state.channelPitchBendSemitones(2) + 48.0f) < 0.0001f);
  state.setPitchBendRange(3, 12, 50);
  assert(fabsf(rangeSemitones(state.pitchBendRange(2)) - 12.5f) < 0.0001f);
  state.setPitchBendRange(1, 7, 0);
  assert(fabsf(rangeSemitones(state.pitchBendRange(1)) - 7.0f) < 0.0001f);
  assert(fabsf(pitchRatio(12.0f) - 2.0f) < 0.0001f);
}

void testVoiceOwnershipAndStealing() {
  State state;
  state.reset();
  state.configureZone(kLowerZone, 6);
  state.setPitchBend(2, 4096);
  assert(state.allocateVoice(0, 2, 60) == 0);
  assert(fabsf(state.voices[0][0].memberBendSemitones - 24.0f) < 0.01f);
  assert(state.allocateVoice(1, 3, 62) == 0);
  assert(fabsf(state.voices[1][0].memberBendSemitones - 0.0f) < 0.0001f);
  assert(state.allocateVoice(0, 3, 64) == 1);
  assert(state.allocateVoice(0, 4, 65) == 2);
  assert(state.allocateVoice(0, 2, 67) == 0);
  assert(state.releaseVoice(0, 2, 60) == -1);
  assert(state.releaseVoice(0, 2, 67) == 0);
  assert(!state.voices[0][0].active);
  const float releasedBend = state.voices[0][0].memberBendSemitones;
  state.setPitchBend(2, 0);
  assert(state.voices[0][0].memberBendSemitones == releasedBend);
  state.setPitchBend(1, kPitchBendMaximum);
  assert(fabsf(state.managerPitchBendSemitones(kLowerZone) - 2.0f) < 0.0001f);
}

void testConventionalAndMpePitchRemainIsolated() {
  assert(fabsf(totalPitchBend(false, 2.0f, -1.0f, 6.0f) - 2.0f) < 0.0001f);
  assert(fabsf(totalPitchBend(true, 2.0f, -1.0f, 6.0f) - 5.0f) < 0.0001f);
}

int main() {
  testFullZoneClaimsAllChannels();
  testEmptyZoneClaimsNothing();
  testRecentZoneConfigurationWins();
  testPitchBendRangesAndEndpoints();
  testVoiceOwnershipAndStealing();
  testConventionalAndMpePitchRemainIsolated();
  return 0;
}
