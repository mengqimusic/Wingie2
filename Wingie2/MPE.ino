float mpe_manager_bend() {
  return mpe_state.managerPitchBendSemitones(wingie_mpe::kLowerZone);
}

// MPE 0xD0 映射常量：深度与曲线硬编码，真机听调定值。
const float kPressureDecayDepthSeconds = 3.0f; // pressure=127 → decay +3s

float mpe_pressure_decay_delta(byte channel) {
  return kPressureDecayDepthSeconds * mpe_state.pressure(channel) / 127.0f;
}

void set_decay_boost(byte ch, byte voice, float seconds) {
  char path[40];
  snprintf(path, sizeof(path), ch ? "/Wingie/right/decay_boost_%u" : "/Wingie/left/decay_boost_%u", voice);
  dsp.setParamValue(path, seconds);
}

void set_side_decay_boost(byte ch, float seconds) {
  for (byte voice = 0; voice < wingie_mpe::kVoiceCount; voice++) set_decay_boost(ch, voice, seconds);
}

void reset_voice_expressions(byte ch) {
  for (byte voice = 0; voice < wingie_mpe::kVoiceCount; voice++) {
    set_decay_boost(ch, voice, 0.0f);
  }
}

// 声部表情源通道：MPE voice 用 owner channel；conventional 用该侧路由通道。
byte voice_expression_channel(byte ch, byte voice) {
  const wingie_mpe::VoiceState &state = mpe_state.voices[ch][voice];
  return state.channel != 0 ? state.channel : conventionalPitchChannel[ch];
}

// 单音（String/Bar）表情源通道：MPE mono owner 优先，否则该侧路由通道。
byte mono_expression_source(byte ch) {
  return mpeMonoState[ch].active ? mpeMonoState[ch].channel : conventionalPitchChannel[ch];
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

// RATIO 复音：声部 v 拥有共鸣器 {3v, 3v+1, 3v+2}，走 cave 通路直写 cave_freq。
void apply_ratio_voice_pitch(byte ch, byte voice) {
  const wingie_mpe::VoiceState &state = mpe_state.voices[ch][voice];
  const float fundamental = configured_note_frequency(state.note) * wingie_mpe::pitchRatio(poly_total_bend(ch, voice));
  for (byte k = 0; k < 3; k++) {
    const byte index = 3 * voice + k;
    const float frequency = max(static_cast<float>(wingie_config::kRatioFrequencyMin),
                                min(static_cast<float>(wingie_config::kRatioFrequencyMax),
                                    fundamental * ratio_profile.ratios[index]));
    cm_freq_set(ch, index, frequency);
  }
}

void apply_all_ratio_voice_pitch(byte ch) {
  for (byte voice = 0; voice < wingie_mpe::kVoiceCount; voice++) apply_ratio_voice_pitch(ch, voice);
}

void unmute_ratio_voice(byte ch, byte voice) {
  for (byte k = 0; k < 3; k++) cm_mute_set(ch, 3 * voice + k, false);
}

void set_ratio_voice_note(byte ch, byte voice, byte noteValue) {
  wingie_mpe::VoiceState &state = mpe_state.voices[ch][voice];
  state.active = false;
  state.channel = 0;
  state.note = noteValue;
  state.memberBendSemitones = 0.0f;
  unmute_ratio_voice(ch, voice);
  apply_ratio_voice_pitch(ch, voice);
}

void cycle_ratio_voice_note(byte ch, byte noteValue) {
  const byte voice = currentPoly[ch];
  currentPoly[ch] = (currentPoly[ch] + 1) % wingie_mpe::kVoiceCount;
  set_ratio_voice_note(ch, voice, noteValue);
}

// 按 Mode[ch] 分发的复音声部入口（POLY / RATIO 共用声部分配与弯音语义）。
// 顺带按声部表情源通道重写 decay boost，保证任何 refresh/重分配路径值一致。
void apply_voice_pitch(byte ch, byte voice) {
  set_decay_boost(ch, voice, mpe_pressure_decay_delta(voice_expression_channel(ch, voice)));
  if (Mode[ch] == RATIO_MODE) apply_ratio_voice_pitch(ch, voice);
  else apply_poly_voice_pitch(ch, voice);
}

void apply_all_voice_pitch(byte ch) {
  for (byte voice = 0; voice < wingie_mpe::kVoiceCount; voice++) apply_voice_pitch(ch, voice);
}

void cycle_voice_note(byte ch, byte noteValue) {
  if (Mode[ch] == RATIO_MODE) cycle_ratio_voice_note(ch, noteValue);
  else cycle_poly_voice_note(ch, noteValue);
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
  if (Mode[ch] == STRING_MODE || Mode[ch] == BAR_MODE) {
    apply_pitched_mode_channel(ch, currentNote[ch]);
  }
}

void refresh_side_pitch(byte ch) {
  if (Mode[ch] == POLY_MODE || Mode[ch] == RATIO_MODE) apply_all_voice_pitch(ch);
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
    reset_voice_expressions(ch);
  }
}

void configure_mpe_zone(byte memberCount) {
  reset_mpe_performance(mpe_state.configureZone(wingie_mpe::kLowerZone, memberCount));
  if (serial_config_ready) refresh_mpe_zone_pitch();
}

void configure_mpe_startup() {
  // MPE 开关是唯一权威：ON 建标准全 Lower Zone，OFF 不建 Zone（全常规路由）。
  configure_mpe_zone(mpe_enabled ? wingie_mpe::kFullZoneMemberCount : 0);
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
  if (Mode[ch] == POLY_MODE || Mode[ch] == RATIO_MODE) {
    const int voice = mpe_state.allocateVoice(ch, channel, pitch);
    if (voice >= 0) {
      if (Mode[ch] == RATIO_MODE) unmute_ratio_voice(ch, voice);
      apply_voice_pitch(ch, voice);
    }
  } else if (Mode[ch] == STRING_MODE || Mode[ch] == BAR_MODE) {
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
    if (Mode[ch] == POLY_MODE || Mode[ch] == RATIO_MODE) {
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

// 0xD0 per-note 表情：按 owner channel 刷新声部 decay boost。
// 直控语义：不要求 voice active——释放后的尾音继续跟随 pressure 回落（抬键过程），
// 归零后自然回到基础 decay；voice 被重新分配后归属随 channel 更新自动切换。
void refresh_mpe_member_expression(byte channel) {
  for (byte ch = 0; ch < 2; ch++) {
    if (Mode[ch] == POLY_MODE || Mode[ch] == RATIO_MODE) {
      for (byte voice = 0; voice < wingie_mpe::kVoiceCount; voice++) {
        const wingie_mpe::VoiceState &state = mpe_state.voices[ch][voice];
        if (state.channel == channel) apply_voice_pitch(ch, voice);
      }
    } else if (mpeMonoState[ch].channel == channel) {
      refresh_mono_pitch(ch);
    }
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
    // MPE 开关关闭时 MCM 同样消费但忽略——开关是 Zone 的唯一权威。
    if (number == 6 && channel == 1 && mpe_enabled) configure_mpe_zone(value);
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
    return true;
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

void handleChannelPressure(byte channel, byte value) {
  mpe_state.setPressure(channel, value);
  if (mpe_state.zoneForChannel(channel) == wingie_mpe::kLowerZone) {
    if (mpe_state.channelIsManager(channel)) return;
    refresh_mpe_member_expression(channel);
    return;
  }
  if (channel == midi_ch_l) set_side_decay_boost(0, mpe_pressure_decay_delta(channel));
  if (channel == midi_ch_r) set_side_decay_boost(1, mpe_pressure_decay_delta(channel));
  if (channel == midi_ch_both) {
    set_side_decay_boost(0, mpe_pressure_decay_delta(channel));
    set_side_decay_boost(1, mpe_pressure_decay_delta(channel));
  }
}
