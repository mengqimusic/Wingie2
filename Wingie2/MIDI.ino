void handleNoteOn (byte channel, byte pitch, byte velocity) {

  //if (pitch < 96) {

  if (velocity) {

    if (channel == midi_ch_l) MIDISetPitch(0, Mode[0], pitch);
    if (channel == midi_ch_r) MIDISetPitch(1, Mode[1], pitch);

    if (channel == midi_ch_both) {
      MIDISetPitch(polyFlip, Mode[polyFlip], pitch);
      polyFlip = !polyFlip;
    }

  }
  //}
}

void MIDISetPitch(int ch, int mode, int pitch) {

  if (mode == STRING_MODE || mode == BAR_MODE) {
    if (!ch) dsp.setParamValue("note0", pitch);
    if (ch) dsp.setParamValue("note1", pitch);
  }

  else if (mode == POLY_MODE) {
    if (currentPoly[ch] == 0) {
      currentPoly[ch] = 1;
      if (!ch) dsp.setParamValue("/Wingie/left/poly_note_0", pitch);
      if (ch) dsp.setParamValue("/Wingie/right/poly_note_0", pitch);
    }
    else if (currentPoly[ch] == 1) {
      currentPoly[ch] = 2;
      if (!ch) dsp.setParamValue("/Wingie/left/poly_note_1", pitch);
      if (ch) dsp.setParamValue("/Wingie/right/poly_note_1", pitch);
    }
    else if (currentPoly[ch] == 2) {
      currentPoly[ch] = 0;
      if (!ch) dsp.setParamValue("/Wingie/left/poly_note_2", pitch);
      if (ch) dsp.setParamValue("/Wingie/right/poly_note_2", pitch);
    }
  }

}

void MIDISetTuning(byte cc, byte value) {
    if (cc == CC_TUNING) {
      if (value == 0) {
        use_alt_tuning = 0;
        alt_tuning_index = -1;
        alt_tuning_set(-1);
        dsp.setParamValue("use_alt_tuning", 0);
        Serial.println("MIDI: Alt tuning disabled");
      } else if (value < 9) {
        int t = value - 1;
        use_alt_tuning = 1;
        dsp.setParamValue("use_alt_tuning", 1);
        alt_tuning_index = t;
        alt_tuning_set(alt_tuning_index);
        tune_caves();
        Serial.printf("MIDI: Alt tuning enabled: %d\n", t);
      }
    }
}

void handleControlChange (byte channel, byte number, byte value) {

  // Serial.printf("MIDI CC -> channel:%hhu, number:%hhu, value:%hhu\n", channel, number, value);

  if (channel == midi_ch_l) MIDISetParam(0, number, value);
  if (channel == midi_ch_r) MIDISetParam(1, number, value);

  if (channel == midi_ch_both) {
    MIDISetParam(0, number, value);
    MIDISetParam(1, number, value);
  }

  if (channel == CC_MIDI_CH_TUNING) {
    MIDISetTuning(number, value);
  }

  if (channel == 14 or channel == 15) { // Cave Frequency Settings
    int ch = channel - 14;
    int cave = oct[ch] + 1;

    if (Mode[ch] == CAVE_MODE) {
      for (int v = 0; v < 9; v++) { // Cave Frequency Adjustment
        if ((number == CC_CAVE_FREQ_1_MSB + v) or (number == CC_CAVE_FREQ_1_LSB + v)) {

          if (number == CC_CAVE_FREQ_1_MSB + v) cave_freq_midi_value[ch][v][MSB] = value;
          else cave_freq_midi_value[ch][v][LSB] = value;

          if (number == CC_CAVE_FREQ_1_MSB + v) {
            int midi_value_14bit = (cave_freq_midi_value[ch][v][MSB] << 7) | cave_freq_midi_value[ch][v][LSB];
            midi_value_14bit = max(midi_value_14bit, CAVE_LOWEST_FREQ);
            midi_value_14bit = min(midi_value_14bit, CAVE_HIGHEST_FREQ);

            cm_freq[ch][cave][v] = midi_value_14bit;
            cm_freq_set(ch, v, cm_freq[ch][cave][v]);
            //cave_midi_set[ch] = true;
          }
        }
      }
    }
  }

  if (channel == 16) { // Global Settings

    if (number == CC_MIDI_CH_L) midi_ch_l = value; dirty[0] = true;
    if (number == CC_MIDI_CH_R) midi_ch_r = value; dirty[1] = true;
    if (number == CC_MIDI_CH_BOTH) midi_ch_both = value; dirty[2] = true;

    if (number == CC_A3_FREQ_MSB || number == CC_A3_FREQ_LSB) {

      if (number == CC_A3_FREQ_MSB) a3_freq_midi_value[MSB] = value;
      if (number == CC_A3_FREQ_LSB) a3_freq_midi_value[LSB] = value;

      if (number == CC_A3_FREQ_MSB) {
        int midi_value_14bit = (a3_freq_midi_value[MSB] << 7) | a3_freq_midi_value[LSB];
        float freq_offset = midi_value_14bit / 100. - 81.92;

        a3_freq = 440. + freq_offset;
        dsp.setParamValue("a3_freq", a3_freq);
        dirty[3] = true;
      }
    }
  }

}

void MIDISetParam(int ch, byte number, byte value) {

  if (number == CC_MODE) {
    int modeFromMIDI = (value >> 5);
    if (Mode[ch] != modeFromMIDI) {
      Mode[ch] = modeFromMIDI;
      modeChangingFromMIDI[ch] = true;
    }
  }

  if (number == CC_MIX or number == CC_MIX + 32) {
    realtime_value_valid[MIX] = false;
    potValSampled[MIX] = potValRealtime[MIX];

    if (number == CC_MIX) midiVal[ch][MIX][MSB] = value;
    else midiVal[ch][MIX][LSB] = value;

    if (number == CC_MIX) {
      int midiVal14Bit = (midiVal[ch][MIX][MSB] << 7) | midiVal[ch][MIX][LSB];
      float v = midiVal14Bit / 16383.;
      if (!ch) dsp.setParamValue("mix0", v);
      if (ch) dsp.setParamValue("mix1", v);
    }
  }

  if (number == CC_DECAY or number == CC_DECAY + 32) {
    realtime_value_valid[DECAY] = false;
    potValSampled[DECAY] = potValRealtime[DECAY];

    if (number == CC_DECAY) midiVal[ch][DECAY][MSB] = value;
    else midiVal[ch][DECAY][LSB] = value;

    if (number == CC_DECAY) {
      int midiVal14Bit = (midiVal[ch][DECAY][MSB] << 7) | midiVal[ch][DECAY][LSB];
      float v = (midiVal14Bit / 16383.) * 9.9 + 0.1;
      v = fscale(0.1, 10., 0.1, 10., v, -3.25);
      if (!ch && !startup) dsp.setParamValue("/Wingie/left/decay", v);
      if (ch && !startup) dsp.setParamValue("/Wingie/right/decay", v);
    }
  }

  if (number == CC_VOL or number == CC_VOL + 32) {
    realtime_value_valid[VOL] = false;
    potValSampled[VOL] = potValRealtime[VOL];

    if (number == CC_VOL) midiVal[ch][VOL][MSB] = value;
    else midiVal[ch][VOL][LSB] = value;

    if (number == CC_VOL) {
      int midiVal14Bit = (midiVal[ch][VOL][MSB] << 7) | midiVal[ch][VOL][LSB];
      float v = midiVal14Bit / 16383.;
      if (!ch) dsp.setParamValue("volume0", v);
      if (ch) dsp.setParamValue("volume1", v);
    }
  }

}
