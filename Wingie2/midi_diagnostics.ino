#if MIDI_DIAGNOSTICS
struct MidiDiagnosticChannel {
  uint32_t noteOnCount;
  uint32_t noteOffCount;
  byte lastPitch;
  byte lastVelocity;
};

struct MidiDiagnosticState {
  MidiDiagnosticChannel channel[2];
  uint32_t readCalls;
  uint32_t parsedMessages;
  uint32_t parseErrors;
  byte lastError;
  int maxRxAvailable;
  unsigned long startedAt;
};

MidiDiagnosticState midiDiagnostic;

int diagnosticChannelIndex(byte channel) {
  if (channel == 1) return 0;
  if (channel == 2) return 1;
  return -1;
}

void resetMidiDiagnostics() {
  memset(&midiDiagnostic, 0, sizeof(midiDiagnostic));
  midiDiagnostic.startedAt = millis();
}

void recordMidiNoteOn(byte channel, byte pitch, byte velocity) {
  int index = diagnosticChannelIndex(channel);
  if (index < 0) return;
  midiDiagnostic.channel[index].noteOnCount++;
  midiDiagnostic.channel[index].lastPitch = pitch;
  midiDiagnostic.channel[index].lastVelocity = velocity;
}

void recordMidiNoteOff(byte channel, byte pitch, byte velocity) {
  int index = diagnosticChannelIndex(channel);
  if (index < 0) return;
  midiDiagnostic.channel[index].noteOffCount++;
  midiDiagnostic.channel[index].lastPitch = pitch;
  midiDiagnostic.channel[index].lastVelocity = velocity;
}

void handleMidiError(int8_t errorCode) {
  midiDiagnostic.parseErrors++;
  midiDiagnostic.lastError = byte(errorCode);
}

void printMidiDiagnostics() {
  Serial.printf("MIDI_DIAG elapsed_ms=%lu read_calls=%lu parsed=%lu errors=%lu last_error=%u rx_high_water=%d\n",
                millis() - midiDiagnostic.startedAt,
                (unsigned long)midiDiagnostic.readCalls,
                (unsigned long)midiDiagnostic.parsedMessages,
                (unsigned long)midiDiagnostic.parseErrors,
                midiDiagnostic.lastError,
                midiDiagnostic.maxRxAvailable);
  for (int ch = 0; ch < 2; ch++) {
    Serial.printf("MIDI_DIAG ch=%d on=%lu off=%lu last_pitch=%u last_velocity=%u mode=%d current_poly=%d\n",
                  ch + 1,
                  (unsigned long)midiDiagnostic.channel[ch].noteOnCount,
                  (unsigned long)midiDiagnostic.channel[ch].noteOffCount,
                  midiDiagnostic.channel[ch].lastPitch,
                  midiDiagnostic.channel[ch].lastVelocity,
                  Mode[ch], currentPoly[ch]);
  }
  Serial.printf("MIDI_DIAG left_note=%.1f left_poly=%.1f,%.1f,%.1f\n",
                dsp.getParamValue("note0"),
                dsp.getParamValue("/Wingie/left/poly_note_0"),
                dsp.getParamValue("/Wingie/left/poly_note_1"),
                dsp.getParamValue("/Wingie/left/poly_note_2"));
  Serial.printf("MIDI_DIAG right_note=%.1f right_poly=%.1f,%.1f,%.1f\n",
                dsp.getParamValue("note1"),
                dsp.getParamValue("/Wingie/right/poly_note_0"),
                dsp.getParamValue("/Wingie/right/poly_note_1"),
                dsp.getParamValue("/Wingie/right/poly_note_2"));
  Serial.printf("ANTI_FEEDBACK enabled=%.0f energy_limit=%.6f rho_guard=%.6f\n",
                dsp.getParamValue("anti_feedback_enabled"),
                dsp.getParamValue("anti_feedback_energy_limit"),
                dsp.getParamValue("anti_feedback_rho_guard"));
}

void serviceMidiDiagnosticsByte(char value) {
  switch (value) {
    case 'r':
      resetMidiDiagnostics();
      Serial.println("MIDI_DIAG reset");
      break;
    case 'p':
      printMidiDiagnostics();
      break;
    case '0':
      dsp.setParamValue("anti_feedback_enabled", 0);
      Serial.println("ANTI_FEEDBACK enabled=0");
      break;
    case '1':
      dsp.setParamValue("anti_feedback_enabled", 1);
      Serial.println("ANTI_FEEDBACK enabled=1");
      break;
  }
}

void serviceMidiDiagnostics() {
  int available = Serial2.available();
  if (available > midiDiagnostic.maxRxAvailable) {
    midiDiagnostic.maxRxAvailable = available;
  }
  midiDiagnostic.readCalls++;
  if (MIDI.read()) midiDiagnostic.parsedMessages++;
}
#endif
