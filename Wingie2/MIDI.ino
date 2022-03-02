void handleNoteOn (byte channel, byte pitch, byte velocity) {

  //if (pitch < 96) {

  if (velocity) {

    if (channel == 1 or channel == 2) {
      int kb = channel - 1;
      MIDISetPitch(kb, Mode[kb], pitch);
    }

    if (channel == 3) {
      MIDISetPitch(polyFlip, Mode[polyFlip], pitch);
      polyFlip = !polyFlip;
    }

  }
  //}
}

void MIDISetPitch(int kb, int mode, int pitch) {

  if (mode != POLY_MODE && mode != REQ_MODE) {
    if (!kb) dsp.setParamValue("note0", pitch);
    if (kb) dsp.setParamValue("note1", pitch);
  }

  else if (mode == POLY_MODE) {
    if (currentPoly[kb] == 0) {
      currentPoly[kb] = 1;
      if (!kb) dsp.setParamValue("/Wingie/left/poly_note_0", pitch);
      if (kb) dsp.setParamValue("/Wingie/right/poly_note_0", pitch);
    }
    else if (currentPoly[kb] == 1) {
      currentPoly[kb] = 2;
      if (!kb) dsp.setParamValue("/Wingie/left/poly_note_1", pitch);
      if (kb) dsp.setParamValue("/Wingie/right/poly_note_1", pitch);
    }
    else if (currentPoly[kb] == 2) {
      currentPoly[kb] = 0;
      if (!kb) dsp.setParamValue("/Wingie/left/poly_note_2", pitch);
      if (kb) dsp.setParamValue("/Wingie/right/poly_note_2", pitch);
    }
  }

}

void handleControlChange (byte channel, byte number, byte value) {

  if (channel == 1 or channel == 2) {
    int kb = channel - 1;
    MIDISetParam(kb, number, value);
  }

  if (channel == 3) {
    MIDISetParam(0, number, value);
    MIDISetParam(1, number, value);
  }

}

void MIDISetParam(int kb, byte number, byte value) {

  if (number == MODE_CC) {
    int modeFromMIDI = (value >> 5);
    if (Mode[kb] != modeFromMIDI) {
      Mode[kb] = modeFromMIDI;
      modeChangingFromMIDI[kb] = true;
    }
  }

  if (number == MIX_CC or number == MIX_CC + 32) {
    midiValValid[MIX] = true;
    potValSampled[MIX] = potValRealtime[MIX];

    if (number == MIX_CC) midiVal[kb][MIX][MSB] = value;
    else midiVal[kb][MIX][LSB] = value;

    int midiVal14Bit = (midiVal[kb][MIX][MSB] << 7) | midiVal[kb][MIX][LSB];

    float v = midiVal14Bit / 16383.;
    Serial.println(v);
    if (!kb) dsp.setParamValue("mix0", v);
    if (kb) dsp.setParamValue("mix1", v);
  }

  if (number == DECAY_CC or number == DECAY_CC + 32) {
    midiValValid[DECAY] = true;
    potValSampled[DECAY] = potValRealtime[DECAY];

    if (number == DECAY_CC) midiVal[kb][DECAY][MSB] = value;
    else midiVal[kb][DECAY][LSB] = value;

    int midiVal14Bit = (midiVal[kb][DECAY][MSB] << 7) | midiVal[kb][DECAY][LSB];

    float v = (midiVal14Bit / 16383.) * 9.9 + 0.1;
    v = fscale(0.1, 10., 0.1, 10., v, -3.25);
    if (!kb) dsp.setParamValue("/Wingie/left/decay", v);
    if (kb) dsp.setParamValue("/Wingie/right/decay", v);
  }

  if (number == VOL_CC or number == VOL_CC + 32) {
    midiValValid[VOL] = true;
    potValSampled[VOL] = potValRealtime[VOL];

    if (number == VOL_CC) midiVal[kb][VOL][MSB] = value;
    else midiVal[kb][VOL][LSB] = value;

    int midiVal14Bit = (midiVal[kb][VOL][MSB] << 7) | midiVal[kb][VOL][LSB];

    float v = midiVal14Bit / 16383.;
    if (!kb) dsp.setParamValue("volume0", v);
    if (kb) dsp.setParamValue("volume1", v);
  }

}
