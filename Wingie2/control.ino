void control( void * pvParameters ) {
  Serial.print("control running on core ");
  Serial.println(xPortGetCoreID());

  Wire.begin(17, 18);

  for (int i = 0; i < 2; i++) {
    pinMode(rstn[i], OUTPUT);
    digitalWrite(rstn[i], true);
    pinMode(intn[i], INPUT);
  }

  delay(5);


  //
  // AW9523B Init
  //
  Serial.println("AW9523 : Connecting...");

  aw0.begin();
  aw1.begin();

  Serial.print("[aw0] ID: 0x");
  Serial.println(aw0.readID(), HEX);
  Serial.print("[aw1] ID: 0x");
  Serial.println(aw1.readID(), HEX);

  aw0.setConfig(0, 0xFF); // input mode
  aw0.setConfig(1, 0xFF);
  aw1.setConfig(0, 0xFF);
  aw1.setConfig(1, 0xFF);

  aw0.setInterrupt(0, 0x00); // interrupt on
  aw0.setInterrupt(1, 0x00);
  aw1.setInterrupt(0, 0x00);
  aw1.setInterrupt(1, 0x00);

  aw0.setGpioMode(0, 0xFF); // gpio mode
  aw0.setGpioMode(1, 0xFF);
  aw1.setGpioMode(0, 0xFF);
  aw1.setGpioMode(1, 0xFF);

  attachInterrupt(digitalPinToInterrupt(intn[0]), Pressed0, FALLING);
  attachInterrupt(digitalPinToInterrupt(intn[1]), Pressed1, FALLING);


  //
  // AC101 Init
  //
  Serial.println("AC101 : Connecting...");

  while (not ac.begin()) {
    Serial.println("AC101 : Failed! Trying...");
    delay(100);
  }
  Serial.println("AC101 : Connected");

  acWriteReg(ADC_SRCBST_CTRL, MIC_BOOST);
  acWriteReg(OMIXER_BST1_CTRL, 0x56DB);

  ac.SetVolumeHeadphone(volume);
  ac.SetVolumeSpeaker(0);

  acWriteReg(OMIXER_SR, 0x0102); // 摆正左右通道 : 左 DAC -> 左输出 右 DAC -> 右输出
  acWriteReg(DAC_VOL_CTRL, DAC_VOL); // 左右通道输出音量 A0 = 0dB A4 = 3dB

  source = !bitRead(gpioStats[1], sourcePin);
  acWriteReg(ADC_SRC, sources[source]);

  ac.DumpRegisters();

  //
  // Stuff Init
  //
  dsp.setParamValue("resonator_input_gain", 0.25);
  dsp.setParamValue("resonator_output_gain", 0.25);
  dsp.setParamValue("mode0", Mode[0]);
  dsp.setParamValue("mode1", Mode[1]);

  dsp.setParamValue("env_mode_change_decay", 0.025);

  for (int kb = 0; kb < 2; kb++) {
    for (int i = 0; i < 2; i++) {
      pinMode(ledPin[kb][i], OUTPUT);
      digitalWrite(ledPin[kb][i], !bitRead(ledColor[Mode[kb]], i)); // 模式 LED 控制
    }
  }

  for (int kb = 0; kb < 2; kb++) gpioRead(kb); // key-read

  int oct[2];
  oct[0] = -!bitRead(gpioStats[1], lOctPin[0]) + !bitRead(gpioStats[1], lOctPin[1]);
  oct[1] = -!bitRead(gpioStats[1], rOctPin[0]) + !bitRead(gpioStats[1], rOctPin[1]);

  //
  // Key Init State Read 用来面对，开机时有些键是被按着的情况
  //
  for (int i = 0; i < 16; i++) {
    bool tmp = bitRead(gpioStats[0], i);
    if (i < 8) key[0][7 - i] = tmp;
    else if (i < 12 && i > 7) key[0][11 - i + 8] = tmp;
    else key[1][i - 12] = tmp;
  }

  for (int i = 0; i < 4; i++) {
    bool tmp = bitRead(gpioStats[1], i);;
    key[1][3 - i + 4] = tmp;
  }

  for (int i = 8; i < 12; i++) {
    bool tmp = bitRead(gpioStats[1], i);;
    key[1][11 - i + 8] = tmp;
  }

  for (int kb = 0; kb < 2; kb++) {
    for (int i = 0; i < 12; i++) keyPrev[kb][i] = key[kb][i];
  }

  modeButtonState[0] = bitRead(gpioStats[1], 4);
  modeButtonState[1] = bitRead(gpioStats[1], 7);

  dsp.setParamValue("note0", BASE_NOTE + oct[0] * 12);
  dsp.setParamValue("note1", BASE_NOTE + oct[1] * 12 + 12);
  dsp.setParamValue("left_threshold", 0.4165);
  dsp.setParamValue("right_threshold", 0.4165);

  dsp.setParamValue("/Wingie/left/poly_note_0", 0 + BASE_NOTE + POLY_MODE_NOTE_ADD_L);
  dsp.setParamValue("/Wingie/left/poly_note_1", 4 + BASE_NOTE + POLY_MODE_NOTE_ADD_L);
  dsp.setParamValue("/Wingie/left/poly_note_2", 7 + BASE_NOTE + POLY_MODE_NOTE_ADD_L);
  dsp.setParamValue("/Wingie/right/poly_note_0", 0 + BASE_NOTE + POLY_MODE_NOTE_ADD_R);
  dsp.setParamValue("/Wingie/right/poly_note_1", 4 + BASE_NOTE + POLY_MODE_NOTE_ADD_R);
  dsp.setParamValue("/Wingie/right/poly_note_2", 7 + BASE_NOTE + POLY_MODE_NOTE_ADD_R);


  for (;;) {
    interrupts();
    currentMillis = millis();

    //Serial.println(uxTaskGetStackHighWaterMark(NULL)); // get unused memory

    //
    // startup
    //
    if (startup) {
      if (currentMillis - startupMillis > 25) {
        startupMillis = currentMillis;
        if (volume < 63) {
          volume += 1;
          ac.SetVolumeHeadphone(volume);
        }
        else startup = false;
      }
    }

    //
    // 9523 Anti-Stuck & GPIO Read
    //
    if (currentMillis - routineReadTimer > 500) {
      routineReadTimer = currentMillis;
      for (int kb = 0; kb < 2; kb++) gpioRead(kb);
    }


    //
    // Interface Reading
    //
    for (int i = 0; i < 3; i++) {
      potValRealtime[i] = analogRead(potPin[i]);
      int difference = abs(potValRealtime[i] - potValSampled[i]);
      if (midiValValid[i]) if (difference > slider_movement_detect) midiValValid[i] = false;
    }

    float Mix = potValRealtime[0] / 4095.;
    if (!midiValValid[MIX]) {
      dsp.setParamValue("mix0", Mix);
      dsp.setParamValue("mix1", Mix);
    }

    float Decay = (potValRealtime[1] / 4095.) * 9.9 + 0.1;
    Decay = fscale(0.1, 10., 0.1, 10., Decay, -3.25);
    if (!midiValValid[DECAY]) {
      dsp.setParamValue("/Wingie/left/decay", Decay);
      dsp.setParamValue("/Wingie/right/decay", Decay);
    }

    float Volume = potValRealtime[2] / 4095.;
    if (!midiValValid[VOL]) {
      dsp.setParamValue("volume0", Volume);
      dsp.setParamValue("volume1", Volume);
    }

    bool sourceSwitchPos = !bitRead(gpioStats[1], sourcePin);


    //
    // source change
    //
    if (sourceSwitchPos != source) {
      source = sourceSwitchPos;
      sourceChanged = true;
      ac.SetVolumeHeadphone(0);
      dsp.setParamValue("/Wingie/left/mode_changed", 1);
      dsp.setParamValue("/Wingie/right/mode_changed", 1);
      sourceChangedMillis = currentMillis;
    }

    if (sourceChanged) {
      if (currentMillis - sourceChangedMillis > 5) {
        sourceChanged = false;
        sourceChanged2 = true;
        acWriteReg(ADC_SRC, sources[source]);
        sourceChangedMillis = currentMillis;
      }
    }

    if (sourceChanged2) {
      if (currentMillis - sourceChangedMillis > 50) {
        sourceChanged2 = false;
        ac.SetVolumeHeadphone(volume);
        dsp.setParamValue("/Wingie/left/mode_changed", 0);
        dsp.setParamValue("/Wingie/right/mode_changed", 0);
      }
    }

    //
    // No Interrupts
    //
    noInterrupts();

    //
    // GPIO Read
    //
    if (an[0]) {

      gpioRead(0);

      an[0] = 0;
      keyChanged = true;

      for (int i = 0; i < 16; i++) {
        bool tmp = bitRead(gpioStats[0], i);

        //      Serial.print(tmp);
        //      Serial.print(" ");

        if (i < 8) key[0][7 - i] = tmp;
        else if (i < 12 && i > 7) key[0][11 - i + 8] = tmp;
        else key[1][i - 12] = tmp;
      }

      //Serial.println();

    }

    if (an[1]) {

      gpioRead(1);

      an[1] = 0;
      keyChanged = true;

      for (int i = 0; i < 4; i++) {
        bool tmp = bitRead(gpioStats[1], i);
        key[1][3 - i + 4] = tmp;
      }

      for (int i = 8; i < 12; i++) {
        bool tmp = bitRead(gpioStats[1], i);
        key[1][11 - i + 8] = tmp;
      }

      modeButtonState[0] = bitRead(gpioStats[1], 4);
      modeButtonState[1] = bitRead(gpioStats[1], 7);

    }


    //
    // oct change
    //
    int oct[2];
    oct[0] = -!bitRead(gpioStats[1], lOctPin[0]) + !bitRead(gpioStats[1], lOctPin[1]);
    oct[1] = -!bitRead(gpioStats[1], rOctPin[0]) + !bitRead(gpioStats[1], rOctPin[1]);

    for (int i = 0; i < 2; i++) {
      if (octPrev[i] != oct[i]) {
        octPrev[i] = oct[i];
        switch (i) {
          case 0 :
            dsp.setParamValue("note0", note[0] + BASE_NOTE + oct[0] * 12);
            break;
          case 1 :
            dsp.setParamValue("note1", note[1] + BASE_NOTE + oct[1] * 12 + 12);
            break;
        }
      }
    }

    //
    // mode change
    //
    for (int kb = 0; kb < 2; kb++) {

      if (modeChangingFromKeys[kb] || modeChangingFromMIDI[kb]) {

        if (modeChangingFromKeys[kb]) {
          modeChangingFromKeys[kb] = false;
          if (Mode[kb] < MODE_NUM) Mode[kb] += 1;
          else Mode[kb] = 0;
        }

        if (modeChangingFromMIDI[kb]) {
          modeChangingFromMIDI[kb] = false;
        }

        modeChanged[kb] = true;

        if (!kb) {
          dsp.setParamValue("mode0", Mode[kb]);
          dsp.setParamValue("/Wingie/left/mode_changed", 1);
        }
        if (kb) {
          dsp.setParamValue("mode1", Mode[kb]);
          dsp.setParamValue("/Wingie/right/mode_changed", 1);
        }

        for (int i = 0; i < 2; i++) digitalWrite(ledPin[kb][i], !bitRead(ledColor[Mode[kb]], i)); // 模式 LED 控制

        if (Mode[kb] != REQ_MODE) {
          for (int i = 0; i < 9; i++) {
            muteStatus[kb][i] = false;
            muteControl(kb, i, false);
          }
        }

      }

      if (modeChanged[kb] && currentMillis - modeChangedTimer[kb] > 20) {
        modeChanged[kb] = false;
        if (!kb) dsp.setParamValue("/Wingie/left/mode_changed", 0);
        if (kb) dsp.setParamValue("/Wingie/right/mode_changed", 0);
      }

    }


    // 用 state change 来触发动作，不在 ISR 中做动作，使连续的两次 false 只做一次动作，消弭 double trigger。

    //
    // Key Change Routine
    //
    if (keyChanged) {
      keyChanged = false;

      for (int kb = 0; kb < 2; kb++) {

        //
        // mode change
        //
        if (!modeButtonState[kb]) modeButtonPressed[kb] = true;
        else if (modeButtonState[kb]) {
          if (modeButtonPressed[kb] && !threshChanged[kb]) modeChangingFromKeys[kb] = true;
          threshChanged[kb] = false;
          modeButtonPressed[kb] = false;
        }


        //
        // note change
        //
        for (int i = 0; i < 12; i++) {
          bool tmp = key[kb][i];
          bitWrite(allKeys[kb], i, !tmp);

          if (key[kb][i] != keyPrev[kb][i]) {
            if (!key[kb][i]) {

              if (modeButtonPressed[kb]) { // Change threshold
                threshChanged[kb] = true;
                float thresh = 0.0833 * i + 0.0833;
                if (!kb) {
                  dsp.setParamValue("left_threshold", thresh);
                };
                if (kb) {
                  dsp.setParamValue("right_threshold", thresh);
                };
              }
              else {
                //if (!kb) dsp.setParamValue("/Wingie/left/mode_changed", 1);
                //if (kb) dsp.setParamValue("/Wingie/right/mode_changed", 1);

                if (firstPress[kb]) {
                  firstPress[kb] = false;

                  if (Mode[kb] != POLY_MODE && Mode[kb] != REQ_MODE) {
                    note[kb] = i;
                    seq[kb][0] = i;
                    seqLen[kb] = 0;
                    playHeadPos[kb] = 0;
                    writeHeadPos[kb] = 0;
                    if (!kb) dsp.setParamValue("note0", note[kb] + BASE_NOTE + oct[kb] * 12);
                    if (kb) dsp.setParamValue("note1", note[kb] + BASE_NOTE + oct[kb] * 12 + 12);
                  }
                }
                else { // Not First Press
                  if (Mode[kb] != POLY_MODE && Mode[kb] != REQ_MODE) {
                    note[kb] = i;
                    writeHeadPos[kb] += 1;
                    seqLen[kb] += 1;
                    seq[kb][writeHeadPos[kb]] = i;
                    if (!kb) dsp.setParamValue("note0", note[kb] + BASE_NOTE + oct[kb] * 12);
                    if (kb) dsp.setParamValue("note1", note[kb] + BASE_NOTE + oct[kb] * 12 + 12);
                  }
                }

                if (Mode[kb] == REQ_MODE) {
                  if (i < 4 || i > 6) {
                    int key;
                    if (i > 6) key = i - 3;
                    else key = i;
                    muteStatus[kb][key] = !muteStatus[kb][key];
                    muteControl(kb, key, muteStatus[kb][key]);
                  }
                }

                if (Mode[kb] == POLY_MODE) {
                  if (currentPoly[kb] == 0) {
                    currentPoly[kb] = 1;
                    if (!kb) dsp.setParamValue("/Wingie/left/poly_note_0", i + BASE_NOTE + oct[kb] * 12 + POLY_MODE_NOTE_ADD_L);
                    if (kb) dsp.setParamValue("/Wingie/right/poly_note_0", i + BASE_NOTE + oct[kb] * 12 + POLY_MODE_NOTE_ADD_R);
                  }
                  else if (currentPoly[kb] == 1) {
                    currentPoly[kb] = 2;
                    if (!kb) dsp.setParamValue("/Wingie/left/poly_note_1", i + BASE_NOTE + oct[kb] * 12 + POLY_MODE_NOTE_ADD_L);
                    if (kb) dsp.setParamValue("/Wingie/right/poly_note_1", i + BASE_NOTE + oct[kb] * 12 + POLY_MODE_NOTE_ADD_R);
                  }
                  else if (currentPoly[kb] == 2) {
                    currentPoly[kb] = 0;
                    if (!kb) dsp.setParamValue("/Wingie/left/poly_note_2", i + BASE_NOTE + oct[kb] * 12 + POLY_MODE_NOTE_ADD_L);
                    if (kb) dsp.setParamValue("/Wingie/right/poly_note_2", i + BASE_NOTE + oct[kb] * 12 + POLY_MODE_NOTE_ADD_R);
                  }
                }

              } // !modeButtonPressed[kb]
            } // Key Press Action End

            else { // Key Release Action Start
              if (!allKeys[kb]) firstPress[kb] = true;
              //if (!kb) dsp.setParamValue("/Wingie/left/mode_changed", 0);
              //if (kb) dsp.setParamValue("/Wingie/right/mode_changed", 0);
            } // Key Release Action End

          }
          keyPrev[kb][i] = key[kb][i];
        }
      }
    }

    //
    // Tap Sequencer
    //
    trig[0] = dsp.getParamValue("/Wingie/left_trig");
    trig[1] = dsp.getParamValue("/Wingie/right_trig");

    for (int kb = 0; kb < 2; kb++) {
      if (seqLen[kb]) {
        if (trig[kb] && !trigged[kb]) {
          trigged[kb] = true;
          if (playHeadPos[kb] < seqLen[kb]) playHeadPos[kb] += 1;
          else playHeadPos[kb] = 0;
          note[kb] = seq[kb][playHeadPos[kb]];
          if (!kb) dsp.setParamValue("note0", note[kb] + BASE_NOTE + oct[kb] * 12);
          if (kb) dsp.setParamValue("note1", note[kb] + BASE_NOTE + oct[kb] * 12 + 12);
          if (!kb) dsp.setParamValue("/Wingie/left/mode_changed", 1);
          if (kb) dsp.setParamValue("/Wingie/right/mode_changed", 1);
        }
      }
      if (!trig[kb] && trigged[kb]) {
        trigged[kb] = false;
        if (!kb) dsp.setParamValue("/Wingie/left/mode_changed", 0);
        if (kb) dsp.setParamValue("/Wingie/right/mode_changed", 0);
      }
    }
  }

}

void gpioRead(int kb) {
  gpioStats[kb] = 0;
  for (int p = 0; p < 2; p++) {
    if (!kb) gpioStats[kb] |= (aw0.readPort(p) << (p * 8));
    else gpioStats[kb] |= (aw1.readPort(p) << (p * 8));
  }
}
