float mpe_manager_bend() {
  return mpe_state.managerPitchBendSemitones(wingie_mpe::kLowerZone);
}

float mono_total_bend(byte ch) {
  return wingie_mpe::totalPitchBend(mpeMonoState[ch].channel != 0, conventionalPitchBend[ch],
                                    mpe_manager_bend(), mpeMonoState[ch].memberBendSemitones);
}

float poly_total_bend(byte ch, byte voice) {
  const wingie_mpe::VoiceState &state = mpe_state.voices[ch][voice];
  return wingie_mpe::totalPitchBend(state.channel != 0, conventionalPitchBend[ch],
                                    mpe_manager_bend(), state.memberBendSemitones);
}

void set_poly_voice_dsp(byte ch, byte voice, byte noteValue, float bendSemitones) {
  char notePath[48];
  char ratioPath[48];
  snprintf(notePath, sizeof(notePath), ch ? "/Wingie/right/poly_note_%u" : "/Wingie/left/poly_note_%u", voice);
  snprintf(ratioPath, sizeof(ratioPath), ch ? "/Wingie/right/poly_pitch_ratio_%u" : "/Wingie/left/poly_pitch_ratio_%u", voice);
  dsp.setParamValue(notePath, noteValue);
  dsp.setParamValue(ratioPath, wingie_mpe::pitchRatio(bendSemitones));
}

void apply_poly_voice_pitch(byte ch, byte voice) {
  const wingie_mpe::VoiceState &state = mpe_state.voices[ch][voice];
  set_poly_voice_dsp(ch, voice, state.note, poly_total_bend(ch, voice));
}

void apply_all_poly_voice_pitch(byte ch) {
  for (byte voice = 0; voice < wingie_mpe::kVoiceCount; voice++) apply_poly_voice_pitch(ch, voice);
}

void set_poly_voice_note(byte ch, byte voice, byte noteValue) {
  wingie_mpe::VoiceState &state = mpe_state.voices[ch][voice];
  state.active = false;
  state.channel = 0;
  state.note = noteValue;
  state.memberBendSemitones = 0.0f;
  apply_poly_voice_pitch(ch, voice);
}

void cycle_poly_voice_note(byte ch, byte noteValue) {
  const byte voice = currentPoly[ch];
  currentPoly[ch] = (currentPoly[ch] + 1) % wingie_mpe::kVoiceCount;
  set_poly_voice_note(ch, voice, noteValue);
}

void clear_mpe_mono_assignment(byte ch) {
  mpeMonoState[ch].active = false;
  mpeMonoState[ch].channel = 0;
  mpeMonoState[ch].memberBendSemitones = 0.0f;
  currentPitchBend[ch] = mono_total_bend(ch);
}

void reset_mpe_assignments(byte ch) {
  mpe_state.clearVoiceOwnership(ch);
  clear_mpe_mono_assignment(ch);
}

void refresh_mono_pitch(byte ch) {
  currentPitchBend[ch] = mono_total_bend(ch);
  if (Mode[ch] == STRING_MODE || Mode[ch] == BAR_MODE || Mode[ch] == RATIO_MODE) {
    apply_pitched_mode_channel(ch, currentNote[ch]);
  }
}

void refresh_side_pitch(byte ch) {
  if (Mode[ch] == POLY_MODE) apply_all_poly_voice_pitch(ch);
  else refresh_mono_pitch(ch);
}

void reset_mpe_performance(uint16_t channelMask) {
  for (byte channel = 1; channel <= wingie_mpe::kChannelCount; channel++) {
    if (!(channelMask & wingie_mpe::channelBit(channel))) continue;
    mpe_state.setPitchBend(channel, 0);
    mpe_state.selectRpn(channel, 101, 127);
    mpe_state.selectRpn(channel, 100, 127);
    mpe_state.channels[channel - 1].conventionalRange = {2, 0};
  }
  for (byte ch = 0; ch < 2; ch++) {
    for (byte voice = 0; voice < wingie_mpe::kVoiceCount; voice++) {
      wingie_mpe::VoiceState &state = mpe_state.voices[ch][voice];
      if (!(channelMask & wingie_mpe::channelBit(state.channel))) continue;
      state.active = false;
      state.channel = 0;
      state.memberBendSemitones = 0.0f;
    }
    if (channelMask & wingie_mpe::channelBit(conventionalPitchChannel[ch])) {
      conventionalPitchChannel[ch] = 0;
      conventionalPitchBend[ch] = 0.0f;
    }
    if (channelMask & wingie_mpe::channelBit(mpeMonoState[ch].channel)) clear_mpe_mono_assignment(ch);
    if (serial_config_ready) refresh_side_pitch(ch);
  }
}

void configure_mpe_zone(byte memberCount) {
  reset_mpe_performance(mpe_state.configureZone(wingie_mpe::kLowerZone, memberCount));
  if (serial_config_ready) refresh_mpe_zone_pitch();
}

void configure_mpe_startup() {
  configure_mpe_zone(wingie_mpe::kStartupMemberCount);
}

void initialize_mpe_state() {
  mpe_state.reset();
  memset(mpeMonoState, 0, sizeof(mpeMonoState));
  mpeFlip = false;
}

// 单 Zone 逐音交替：Cave 侧不参与分配，音符全部落到可发声侧；两侧均 Cave 时吞掉。
int8_t mpe_note_side() {
  const bool leftPlayable = Mode[0] != CAVE_MODE;
  const bool rightPlayable = Mode[1] != CAVE_MODE;
  if (!leftPlayable && !rightPlayable) return -1;
  if (!leftPlayable) return 1;
  if (!rightPlayable) return 0;
  const byte side = mpeFlip ? 1 : 0;
  mpeFlip = !mpeFlip;
  return side;
}

bool handle_mpe_note_on(byte channel, byte pitch) {
  if (mpe_state.zoneForChannel(channel) != wingie_mpe::kLowerZone) return false;
  const int8_t side = mpe_note_side();
  if (side < 0) return true;
  const byte ch = static_cast<byte>(side);
  if (Mode[ch] == POLY_MODE) {
    const int voice = mpe_state.allocateVoice(ch, channel, pitch);
    if (voice >= 0) apply_poly_voice_pitch(ch, voice);
  } else if (Mode[ch] == STRING_MODE || Mode[ch] == BAR_MODE || Mode[ch] == RATIO_MODE) {
    mpeMonoState[ch].active = true;
    mpeMonoState[ch].channel = channel;
    mpeMonoState[ch].note = pitch;
    mpeMonoState[ch].memberBendSemitones = mpe_state.memberPitchBendSemitones(channel);
    set_channel_pitch(ch, pitch, mono_total_bend(ch));
  }
  return true;
}

bool handle_mpe_note_off(byte channel, byte pitch) {
  if (mpe_state.zoneForChannel(channel) != wingie_mpe::kLowerZone) return false;
  // ownership 记录 (channel,note)->(side,voice)，Note Off 需在两侧查找归属。
  for (byte ch = 0; ch < 2; ch++) {
    if (Mode[ch] == POLY_MODE) {
      if (mpe_state.releaseVoice(ch, channel, pitch) >= 0) return true;
    } else if (mpeMonoState[ch].active && mpeMonoState[ch].channel == channel && mpeMonoState[ch].note == pitch) {
      mpeMonoState[ch].active = false;
      return true;
    }
  }
  return true;
}

void set_conventional_side_pitch(byte ch, byte channel) {
  conventionalPitchChannel[ch] = channel;
  conventionalPitchBend[ch] = mpe_state.channelPitchBendSemitones(channel);
  refresh_side_pitch(ch);
}

void set_conventional_channel_note(byte ch, byte channel, byte pitch) {
  conventionalPitchChannel[ch] = channel;
  conventionalPitchBend[ch] = mpe_state.channelPitchBendSemitones(channel);
  clear_mpe_mono_assignment(ch);
  set_channel_pitch(ch, pitch, mono_total_bend(ch));
}

void refresh_mpe_zone_pitch() {
  for (byte ch = 0; ch < 2; ch++) {
    for (byte voice = 0; voice < wingie_mpe::kVoiceCount; voice++) {
      wingie_mpe::VoiceState &state = mpe_state.voices[ch][voice];
      if (state.active && !mpe_state.channelIsManager(state.channel)) {
        state.memberBendSemitones = mpe_state.memberPitchBendSemitones(state.channel);
      }
    }
    if (mpeMonoState[ch].active && !mpe_state.channelIsManager(mpeMonoState[ch].channel)) {
      mpeMonoState[ch].memberBendSemitones = mpe_state.memberPitchBendSemitones(mpeMonoState[ch].channel);
    }
    refresh_side_pitch(ch);
  }
}

void refresh_mpe_member_pitch(byte channel) {
  for (byte ch = 0; ch < 2; ch++) {
    for (byte voice = 0; voice < wingie_mpe::kVoiceCount; voice++) {
      wingie_mpe::VoiceState &state = mpe_state.voices[ch][voice];
      if (state.active && state.channel == channel) {
        state.memberBendSemitones = mpe_state.memberPitchBendSemitones(channel);
      }
    }
    if (mpeMonoState[ch].active && mpeMonoState[ch].channel == channel) {
      mpeMonoState[ch].memberBendSemitones = mpe_state.memberPitchBendSemitones(channel);
    }
    refresh_side_pitch(ch);
  }
}

void apply_pitch_bend_range(byte channel, byte controller, byte value) {
  wingie_mpe::PitchBendRange range = mpe_state.pitchBendRange(channel);
  if (controller == 6) range.semitones = value;
  if (controller == 38) range.cents = value;
  mpe_state.setPitchBendRange(channel, range.semitones, range.cents);
  if (mpe_state.zoneForChannel(channel) == wingie_mpe::kLowerZone) {
    refresh_mpe_zone_pitch();
    return;
  }
  for (byte ch = 0; ch < 2; ch++) {
    if (conventionalPitchChannel[ch] == channel) set_conventional_side_pitch(ch, channel);
  }
}

bool handle_mpe_rpn(byte channel, byte number, byte value) {
  if (number == 101 || number == 100) {
    mpe_state.selectRpn(channel, number, value);
    return true;
  }
  if (number != 6 && number != 38) return false;
  if (mpe_state.selectedRpnIs(channel, 0, 6)) {
    // 单 Zone 策略：仅 Ch1 的 MCM 生效；Ch16 Upper MCM 被消费但忽略（见 MPE.md）。
    if (number == 6 && channel == 1) configure_mpe_zone(value);
    return true;
  }
  if (mpe_state.selectedRpnIs(channel, 0, 0)) {
    apply_pitch_bend_range(channel, number, value);
    return true;
  }
  return false;
}

bool handle_mpe_control_change(byte channel, byte number, byte value) {
  if (handle_mpe_rpn(channel, number, value)) return true;
  if (mpe_state.zoneForChannel(channel) != wingie_mpe::kLowerZone) return false;
  if (mpe_state.channelIsManager(channel)) {
    // 单 Manager 全局语义：Ch1 CC 同时作用左右两侧。
    MIDISetParam(0, number, value);
    MIDISetParam(1, number, value);
  }
  return true;
}

void handlePitchBend(byte channel, int bend) {
#if MIDI_DIAGNOSTICS
  recordMidiPitchBend(channel, bend);
#endif
  mpe_state.setPitchBend(channel, bend);
  if (mpe_state.zoneForChannel(channel) == wingie_mpe::kLowerZone) {
    if (mpe_state.channelIsManager(channel)) {
      refresh_side_pitch(0);
      refresh_side_pitch(1);
    } else {
      refresh_mpe_member_pitch(channel);
    }
    return;
  }
  if (channel == midi_ch_l) set_conventional_side_pitch(0, channel);
  if (channel == midi_ch_r) set_conventional_side_pitch(1, channel);
  if (channel == midi_ch_both) {
    set_conventional_side_pitch(0, channel);
    set_conventional_side_pitch(1, channel);
  }
}
