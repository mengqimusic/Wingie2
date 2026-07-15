void control(void *pvParameters) {
  Serial.print("control running on core ");
  Serial.println(xPortGetCoreID());

  //
  // Hardware Init Begin
  //

  Wire.begin(17, 18);

  for (int i = 0; i < 2; i++) {
    pinMode(rstn[i], OUTPUT);
    digitalWrite(rstn[i], true);
    pinMode(intn[i], INPUT);
  }

  delay(5);

  Serial.println("AW9523 : Connecting...");

  if (!aw0.begin(0x58)) Serial.println("AW9523 0 : Connection Failed");
  else Serial.println("AW9523 0 : Connected");

  if (!aw1.begin(0x59)) Serial.println("AW9523 1 : Connection Failed");
  else Serial.println("AW9523 1 : Connected");

  aw0.reset();
  aw1.reset();

  for (int i = 0; i < 16; i++) {  // setup aw9523
    aw0.pinMode(i, INPUT);
    aw0.enableInterrupt(i, true);
    aw1.pinMode(i, INPUT);
    aw1.enableInterrupt(i, true);
  }

  for (int i = 0; i < 16; i += 8) {  // anti-stuck
    bool tmp = aw0.digitalRead(i);
    tmp = aw1.digitalRead(i);
  }

  attachInterrupt(digitalPinToInterrupt(intn[0]), Pressed0, FALLING);
  attachInterrupt(digitalPinToInterrupt(intn[1]), Pressed1, FALLING);

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

  acWriteReg(OMIXER_SR, 0x0102);      // 摆正左右通道 : 左 DAC -> 左输出 右 DAC -> 右输出
  acWriteReg(DAC_VOL_CTRL, DAC_VOL);  // 左右通道输出音量 A0 = 0dB A4 = 3dB

  source = !aw1.digitalRead(sourcePin);
  acWriteReg(ADC_SRC, sources[source]);

  ac.DumpRegisters();

  //
  // Hardware Init End
  //

  //
  // Software Init Begin
  //

  // Preferences Section Begin

  prefs.begin("counter");
  unsigned int counter = prefs.getUInt("counter", 0);
  counter++;
  Serial.printf("这是此小羽第 %u 次启动。\n", counter);
  prefs.putUInt("counter", counter);
  prefs.end();

  prefs.begin("settings", RO_MODE);

  bool nvs_init = prefs.isKey("nvs_init");

  if (!nvs_init) {
    prefs.end();
    prefs.begin("settings", RW_MODE);

    prefs.putUChar("midi_ch_l", 1);
    prefs.putUChar("midi_ch_r", 2);
    prefs.putUChar("midi_ch_both", 3);
    prefs.putFloat("a3_freq_offset", 0.);
    prefs.putFloat("pre_clip_gain", 0.2475);
    prefs.putFloat("post_clip_gain", 0.825);
    prefs.putFloat("left_thresh", 0.4125);
    prefs.putFloat("right_thresh", 0.4125);
    prefs.putUChar("left_mode", 0);
    prefs.putUChar("right_mode", 0);
    prefs.putUChar("use_alt_tuning", 0);
    prefs.putChar("alt_tuning_idx", -1);
    prefs.putBool("unq_caves_store", false);
    for (int ch = 0; ch < 2; ch++) {
      for (int cave = 0; cave < 3; cave++) {
        for (int v = 0; v < 9; v++) {
          char buff[100];

          if (!ch) snprintf(buff, sizeof(buff), "l_cf_%d_%d", cave, v);
          else snprintf(buff, sizeof(buff), "r_cf_%d_%d", cave, v);
          const char *addr = buff;
          prefs.putUShort(addr, cm_freq_prev[ch][cave][v]);

          if (!ch) snprintf(buff, sizeof(buff), "l_cms_%d_%d", cave, v);
          else snprintf(buff, sizeof(buff), "r_cms_%d_%d", cave, v);
          addr = buff;
          prefs.putBool(addr, cm_ms_prev[ch][cave][v]);
        }
      }
    }
    prefs.putBool("nvs_init", true);
    prefs.end();
    Serial.println("NVS initialized");
    prefs.begin("settings", RO_MODE);
  }

  midi_ch_l = prefs.getUChar("midi_ch_l");
  midi_ch_r = prefs.getUChar("midi_ch_r");
  midi_ch_both = prefs.getUChar("midi_ch_both");
  Serial.printf("midi_ch_l = %d / midi_ch_r = %d / midi_ch_both = %d\n", midi_ch_l, midi_ch_r, midi_ch_both);
  float a3_freq_offset = prefs.getFloat("a3_freq_offset", 99);
  a3_freq = 440. + a3_freq_offset;
  dsp.setParamValue("a3_freq", a3_freq);
  Serial.printf("a3_freq = %.2f\n", a3_freq);
  pre_clip_gain = prefs.getFloat("pre_clip_gain", 0);
  dsp.setParamValue("pre_clip_gain", pre_clip_gain);
  Serial.printf("pre_clip_gain = %.4f\n", pre_clip_gain);
  post_clip_gain = prefs.getFloat("post_clip_gain", 0);
  dsp.setParamValue("post_clip_gain", post_clip_gain);
  Serial.printf("post_clip_gain = %.4f\n", post_clip_gain);

  left_thresh = prefs.getFloat("left_thresh", 0);
  dsp.setParamValue("left_thresh", left_thresh);
  Serial.printf("left_thresh = %.4f\n", left_thresh);

  right_thresh = prefs.getFloat("right_thresh", 0);
  dsp.setParamValue("right_thresh", right_thresh);
  Serial.printf("right_thresh = %.4f\n", right_thresh);

  for (int ch = 0; ch < 2; ch++) {
    for (int cave = 0; cave < 3; cave++) {
      if (!load_cave_bank_from_preferences(prefs, ch, cave, false)) {
        cave_storage_migration_pending = true;
        for (int voice = 0; voice < 9; voice++) {
          char key[16];
          snprintf(key, sizeof(key), ch ? "r_cf_%d_%d" : "l_cf_%d_%d", cave, voice);
          float canonical = 0.0f;
          wingie_config::canonicalizeCaveFrequency(prefs.getUShort(key), canonical);
          cm_freq[ch][cave][voice] = canonical;
          cm_freq_prev[ch][cave][voice] = canonical;
          snprintf(key, sizeof(key), ch ? "r_cms_%d_%d" : "l_cms_%d_%d", cave, voice);
          cm_ms[ch][cave][voice] = prefs.getBool(key);
          cm_ms_prev[ch][cave][voice] = cm_ms[ch][cave][voice];
        }
      }
    }
  }

  unq_caves_store = prefs.getBool("unq_caves_store");
  Serial.printf("unq_caves_store = %d\n", unq_caves_store);
  if (unq_caves_store) {
    for (int ch = 0; ch < 2; ch++) {
      for (int cave = 0; cave < 3; cave++) {
        if (!load_cave_bank_from_preferences(prefs, ch, cave, true)) {
          cave_storage_migration_pending = true;
          unquantized_caves_dirty = true;
          for (int voice = 0; voice < 9; voice++) {
            char key[16];
            snprintf(key, sizeof(key), ch ? "r_cf_unq_%d_%d" : "l_cf_unq_%d_%d", cave, voice);
            float canonical = 0.0f;
            wingie_config::canonicalizeCaveFrequency(prefs.getUShort(key), canonical);
            cm_freq_stored_unq[ch][cave][voice] = canonical;
          }
        }
      }
    }
  }

  Mode[0] = prefs.getUChar("left_mode");
  set_channel_dsp_mode(0);
  Serial.printf("left_mode = %d\n", Mode[0]);

  Mode[1] = prefs.getUChar("right_mode");
  set_channel_dsp_mode(1);
  Serial.printf("right_mode = %d\n", Mode[1]);

  use_alt_tuning = prefs.getUChar("use_alt_tuning", 0);
  dsp.setParamValue("use_alt_tuning", use_alt_tuning);
  alt_tuning_index = prefs.getChar("alt_tuning_idx", -1);
  Serial.printf("use_alt_tuning = %d, alt_tuning_index = %d\n", use_alt_tuning, alt_tuning_index);

  load_ratio_profile_from_preferences();

  prefs.end();

  // Preferences Section End

  dsp.setParamValue("resonator_input_gain", 0.25);
  dsp.setParamValue("env_mode_change_decay", 0.025);

  for (int ch = 0; ch < 2; ch++) {
    for (int i = 0; i < 2; i++) {
      pinMode(ledPin[ch][i], OUTPUT);
    }
    set_mode_led(ch);
  }

  oct[0] = -!aw1.digitalRead(lOctPin[0]) + !aw1.digitalRead(lOctPin[1]);
  oct[1] = -!aw1.digitalRead(rOctPin[0]) + !aw1.digitalRead(rOctPin[1]);

  //
  // Key Init State Read 用来面对，开机时有些键是被按着的情况
  //
  for (int i = 0; i < 16; i++) {
    bool tmp = aw0.digitalRead(i);
    if (i < 8) key[0][7 - i] = tmp;
    else if (i < 12 && i > 7) key[0][11 - i + 8] = tmp;
    else key[1][i - 12] = tmp;
  }

  for (int i = 0; i < 4; i++) {
    bool tmp = aw1.digitalRead(i);
    key[1][3 - i + 4] = tmp;
  }

  for (int i = 8; i < 12; i++) {
    bool tmp = aw1.digitalRead(i);
    key[1][11 - i + 8] = tmp;
  }

  for (int ch = 0; ch < 2; ch++) {
    for (int i = 0; i < 12; i++) keyPrev[ch][i] = key[ch][i];
  }

  // handle mode button holds for alternate tuning settings or preference clearing
  //    - left button held: enable alternate tuning and select scale bases on slider positions (base 2)
  //    - right button held: disable alternate tuning and revert to standard tuning
  //    - both buttons held: clear prefs and restart clean

  modeButtonState[0] = aw1.digitalRead(4);
  modeButtonState[1] = aw1.digitalRead(7);

  if (!modeButtonState[0] && !modeButtonState[1]) {
    // both buttons -- CLEAR ALL PREFS AND REBOOT
    Serial.printf("Clearing prefs!\n");
    prefs.begin("settings", RW_MODE);
    prefs.clear();
    prefs.end();
    Serial.printf("Restarting device...\n");
    ESP.restart();
  } else if (!modeButtonState[0]) {
    // left button only
    Serial.printf("Enabling/resetting alt tuning\n");
    set_tuning_index(get_int_from_sliders(), CONFIG_ORIGIN_HARDWARE);
  } else if (!modeButtonState[1]) {
    // right button only
    set_tuning_index(-1, CONFIG_ORIGIN_HARDWARE);
  }

  if (use_alt_tuning != 0 && alt_tuning_index != -1) {
    alt_tuning_set(alt_tuning_index);
    tune_caves(CONFIG_ORIGIN_STARTUP);
  }

  Serial.printf("Using %s tuning\n", use_alt_tuning == 0 ? "standard" : "alternate");

  set_channel_note(0, BASE_NOTE + oct[0] * 12, CONFIG_ORIGIN_STARTUP);
  set_channel_note(1, BASE_NOTE + oct[1] * 12 + 12, CONFIG_ORIGIN_STARTUP);
  if (Mode[0] == CAVE_MODE || Mode[0] == RATIO_MODE) apply_current_mode_parameters(0);
  if (Mode[1] == CAVE_MODE || Mode[1] == RATIO_MODE) apply_current_mode_parameters(1);

  dsp.setParamValue("/Wingie/left/poly_note_0", 0 + BASE_NOTE + POLY_MODE_NOTE_ADD_L);
  dsp.setParamValue("/Wingie/left/poly_note_1", 4 + BASE_NOTE + POLY_MODE_NOTE_ADD_L);
  dsp.setParamValue("/Wingie/left/poly_note_2", 7 + BASE_NOTE + POLY_MODE_NOTE_ADD_L);
  dsp.setParamValue("/Wingie/right/poly_note_0", 0 + BASE_NOTE + POLY_MODE_NOTE_ADD_R);
  dsp.setParamValue("/Wingie/right/poly_note_1", 4 + BASE_NOTE + POLY_MODE_NOTE_ADD_R);
  dsp.setParamValue("/Wingie/right/poly_note_2", 7 + BASE_NOTE + POLY_MODE_NOTE_ADD_R);

  dsp.setParamValue("/Wingie/left/decay", 0.1);  // 最小 Startup Decay 避免开机声音过大
  dsp.setParamValue("/Wingie/right/decay", 0.1);

  serial_config_ready = true;

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
        } else startup = false;
      }
    }

    //
    // 9523 Anti-Stuck
    //
    if (currentMillis - routineReadTimer > 500) {
      routineReadTimer = currentMillis;
      for (int i = 0; i < 16; i += 8) {
        bool tmp = aw0.digitalRead(i);
        tmp = aw1.digitalRead(i);
      }
    }


    //
    // Interface Reading
    //
    for (int i = 0; i < 3; i++) {
      potValRealtime[i] = analogRead(potPin[i]);
      int difference = abs(potValRealtime[i] - potValSampled[i]);
      if (!realtime_value_valid[i])
        if (difference > slider_movement_detect) realtime_value_valid[i] = true;
    }

    float Mix = potValRealtime[0] / 4095.;
    if (realtime_value_valid[MIX]) {
      set_channel_performance_parameter(0, PERFORMANCE_MIX, Mix, CONFIG_ORIGIN_HARDWARE, false);
      set_channel_performance_parameter(1, PERFORMANCE_MIX, Mix, CONFIG_ORIGIN_HARDWARE, false);
    }

    float Decay = (potValRealtime[1] / 4095.) * 9.9 + 0.1;
    Decay = fscale(0.1, 10., 0.1, 10., Decay, -3.25);
    if (realtime_value_valid[DECAY] && !startup) {
      set_channel_performance_parameter(0, PERFORMANCE_DECAY, Decay, CONFIG_ORIGIN_HARDWARE, false);
      set_channel_performance_parameter(1, PERFORMANCE_DECAY, Decay, CONFIG_ORIGIN_HARDWARE, false);
    }

    float Volume = potValRealtime[2] / 4095.;
    if (realtime_value_valid[VOL]) {
      set_channel_performance_parameter(0, PERFORMANCE_VOLUME, Volume, CONFIG_ORIGIN_HARDWARE, false);
      set_channel_performance_parameter(1, PERFORMANCE_VOLUME, Volume, CONFIG_ORIGIN_HARDWARE, false);
    }

    bool sourceSwitchPos = !aw1.digitalRead(sourcePin);


    //
    // source change
    //
    if (sourceSwitchPos != source) {
      source = sourceSwitchPos;
      mark_config_state_changed(CONFIG_ORIGIN_HARDWARE);
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
    // oct change
    //
    oct[0] = -!aw1.digitalRead(lOctPin[0]) + !aw1.digitalRead(lOctPin[1]);
    oct[1] = -!aw1.digitalRead(rOctPin[0]) + !aw1.digitalRead(rOctPin[1]);

    for (int ch = 0; ch < 2; ch++) {
      if (octPrev[ch] != oct[ch]) {
        octPrev[ch] = oct[ch];
        set_channel_note(ch, note[ch] + BASE_NOTE + oct[ch] * 12 + (ch ? 12 : 0));
      }

      if (Mode[ch] == CAVE_MODE) {
        int cave = oct[ch] + 1;
        if (!ch) dsp.setParamValue("/Wingie/left/mode_changed", 1);
        if (ch) dsp.setParamValue("/Wingie/right/mode_changed", 1);
        duck_env_triggered[ch] = true;
        duck_env_init_timer[ch] = currentMillis;
        apply_cave_bank_to_dsp(ch, cave);
      }
    }

    //
    // mode change
    //
    for (int ch = 0; ch < 2; ch++) {

      if (modeChangingFromKeys[ch]) {
        modeChangingFromKeys[ch] = false;
        const int nextMode = Mode[ch] < MODE_NUM ? Mode[ch] + 1 : 0;
        set_channel_mode(ch, nextMode, CONFIG_ORIGIN_HARDWARE);
      }

      if (duck_env_triggered[ch] && currentMillis - duck_env_init_timer[ch] > 20) {
        duck_env_triggered[ch] = false;
        if (!ch) dsp.setParamValue("/Wingie/left/mode_changed", 0);
        if (ch) dsp.setParamValue("/Wingie/right/mode_changed", 0);
      }
    }


    //
    // Key Read
    //
    if (an[0]) {

      an[0] = 0;
      keyChanged = true;

      for (int i = 0; i < 16; i++) {
        bool tmp = aw0.digitalRead(i);

        //      Serial.print(tmp);
        //      Serial.print(" ");

        if (i < 8) key[0][7 - i] = tmp;
        else if (i < 12 && i > 7) key[0][11 - i + 8] = tmp;
        else key[1][i - 12] = tmp;
      }

      //Serial.println();
    }

    if (an[1]) {

      an[1] = 0;
      keyChanged = true;

      for (int i = 0; i < 4; i++) {
        bool tmp = aw1.digitalRead(i);
        key[1][3 - i + 4] = tmp;
      }

      for (int i = 8; i < 12; i++) {
        bool tmp = aw1.digitalRead(i);
        key[1][11 - i + 8] = tmp;
      }

      modeButtonState[0] = aw1.digitalRead(4);
      modeButtonState[1] = aw1.digitalRead(7);
    }

    // 用 state change 来触发动作，不在 ISR 中做动作，使连续的两次 false 只做一次动作，消弭 double trigger。

    //
    // Key Change Routine
    //
    if (keyChanged) {
      keyChanged = false;

      for (int ch = 0; ch < 2; ch++) {

        //
        // mode change
        //
        if (!modeButtonState[ch]) modeButtonPressed[ch] = true;
        else if (modeButtonState[ch]) {
          if (modeButtonPressed[ch] && !threshChanged[ch] && !stuff_saved) modeChangingFromKeys[ch] = true;
          threshChanged[ch] = false;
          modeButtonPressed[ch] = false;
        }


        //
        // note change
        //
        for (int i = 0; i < 12; i++) {
          bool tmp = key[ch][i];
          bitWrite(allKeys[ch], i, !tmp);

          if (key[ch][i] != keyPrev[ch][i]) {
            if (!key[ch][i]) {

              if (modeButtonPressed[0]) {  // Change threshold
                threshChanged[0] = true;
                if (!ch) {
                  set_channel_threshold(0, 0.0825f * i + 0.0825f, CONFIG_ORIGIN_HARDWARE);
                }
                if (ch) {
                  set_channel_threshold(1, 0.0825f * i + 0.0825f, CONFIG_ORIGIN_HARDWARE);
                }
              }

              else if (modeButtonPressed[1]) {  // Change gain
                threshChanged[1] = true;
                if (!ch) {
                  set_clip_gain(false, 0.0825f * i + 0.0825f, CONFIG_ORIGIN_HARDWARE);
                }
                if (ch) {
                  set_clip_gain(true, 0.055f * i + 0.385f, CONFIG_ORIGIN_HARDWARE);
                }
              }

              else {
                //if (!ch) dsp.setParamValue("/Wingie/left/mode_changed", 1);
                //if (ch) dsp.setParamValue("/Wingie/right/mode_changed", 1);

                if (firstPress[ch]) {
                  firstPress[ch] = false;

                  if (Mode[ch] != POLY_MODE && Mode[ch] != CAVE_MODE) {
                    note[ch] = i;
                    tapSequence[ch].reset(uint8_t(i));
                    set_channel_note(ch, note[ch] + BASE_NOTE + oct[ch] * 12 + (ch ? 12 : 0));
                  }
                } else {  // Not First Press
                  if (Mode[ch] != POLY_MODE && Mode[ch] != CAVE_MODE) {
                    note[ch] = i;
                    tapSequence[ch].append(uint8_t(i));
                    set_channel_note(ch, note[ch] + BASE_NOTE + oct[ch] * 12 + (ch ? 12 : 0));
                  }
                }

                if (Mode[ch] == CAVE_MODE) {
                  int cave = oct[ch] + 1;
                  if (i < 4 || i > 6) {
                    int v;
                    if (i > 6) v = i - 3;
                    else v = i;
                    if (!(!key[ch][4] || !key[ch][5])) {
                      set_cave_mute(ch, cave, v, !cm_ms[ch][cave][v], CONFIG_ORIGIN_HARDWARE);
                    }
                  }
                  if (i == 6) {  // in CAVE MODE: hold E or F and press F# for "mute all" and "unmute all"
                    int mute_unmute = -!key[ch][4] + !key[ch][5];
                    switch (mute_unmute) {
                      case -1:
                        for (int v = 0; v < 9; v++) {
                          set_cave_mute(ch, cave, v, true, CONFIG_ORIGIN_HARDWARE);
                        }
                        break;
                      case 1:
                        for (int v = 0; v < 9; v++) {
                          set_cave_mute(ch, cave, v, false, CONFIG_ORIGIN_HARDWARE);
                        }
                        break;
                    }
                  }
                }

                if (Mode[ch] == POLY_MODE) {
                  if (currentPoly[ch] == 0) {
                    currentPoly[ch] = 1;
                    if (!ch) dsp.setParamValue("/Wingie/left/poly_note_0", i + BASE_NOTE + oct[ch] * 12 + POLY_MODE_NOTE_ADD_L);
                    if (ch) dsp.setParamValue("/Wingie/right/poly_note_0", i + BASE_NOTE + oct[ch] * 12 + POLY_MODE_NOTE_ADD_R);
                  } else if (currentPoly[ch] == 1) {
                    currentPoly[ch] = 2;
                    if (!ch) dsp.setParamValue("/Wingie/left/poly_note_1", i + BASE_NOTE + oct[ch] * 12 + POLY_MODE_NOTE_ADD_L);
                    if (ch) dsp.setParamValue("/Wingie/right/poly_note_1", i + BASE_NOTE + oct[ch] * 12 + POLY_MODE_NOTE_ADD_R);
                  } else if (currentPoly[ch] == 2) {
                    currentPoly[ch] = 0;
                    if (!ch) dsp.setParamValue("/Wingie/left/poly_note_2", i + BASE_NOTE + oct[ch] * 12 + POLY_MODE_NOTE_ADD_L);
                    if (ch) dsp.setParamValue("/Wingie/right/poly_note_2", i + BASE_NOTE + oct[ch] * 12 + POLY_MODE_NOTE_ADD_R);
                  }
                }

              }  // !modeButtonPressed[ch]
            }    // Key Press Action End

            else {  // Key Release Action Start
              if (!allKeys[ch]) firstPress[ch] = true;
              //if (!ch) dsp.setParamValue("/Wingie/left/mode_changed", 0);
              //if (ch) dsp.setParamValue("/Wingie/right/mode_changed", 0);
            }  // Key Release Action End
          }
          keyPrev[ch][i] = key[ch][i];
        }
      }
    }

    //
    // cave mode adjusting
    //
    for (int ch = 0; ch < 2; ch++) {
      int cave = oct[ch] + 1;

      if (Mode[ch] == CAVE_MODE) {

        if (!key[ch][4] or !key[ch][5]) {
          int adj[2];
          adj[ch] = -!key[ch][4] + !key[ch][5];


          for (int v = 0; v < 9; v++) {

            int k;
            if (v > 3) k = v + 3;
            else k = v;

            if (!key[ch][k]) {

              adj[ch] = adj[ch] * fscale(50, 16000, 1, 20, cm_freq[ch][cave][v], -0.85);  // 指数增长下降

              set_cave_frequency(ch, cave, v, cm_freq[ch][cave][v] + adj[ch],
                                 CONFIG_ORIGIN_HARDWARE);
            }
          }
        }
      }
    }


    //
    // Tap Sequencer
    //
    trig[0] = dsp.getParamValue("/Wingie/left_trig");
    trig[1] = dsp.getParamValue("/Wingie/right_trig");

    for (int ch = 0; ch < 2; ch++) {
      if (trig[ch] != trigStatePrev[ch]) {
        trigStatePrev[ch] = trig[ch];
        mark_config_state_changed(CONFIG_ORIGIN_HARDWARE);
      }
      if (tapSequence[ch].hasCycle()) {
        if (trig[ch] && !trigged[ch]) {
          trigged[ch] = true;
          uint8_t nextNote;
          if (tapSequence[ch].advance(nextNote)) {
            note[ch] = nextNote;
            set_channel_note(ch, note[ch] + BASE_NOTE + oct[ch] * 12 + (ch ? 12 : 0));
            if (!ch) dsp.setParamValue("/Wingie/left/mode_changed", 1);
            if (ch) dsp.setParamValue("/Wingie/right/mode_changed", 1);
          }
        }
      }
      if (!trig[ch] && trigged[ch]) {
        trigged[ch] = false;
        if (!ch) dsp.setParamValue("/Wingie/left/mode_changed", 0);
        if (ch) dsp.setParamValue("/Wingie/right/mode_changed", 0);
      }
    }

    //
    // save routine
    //
    if (modeButtonPressed[0] && modeButtonPressed[1] && !save_routine_flag && !stuff_saved) {
      save_routine_flag = true;
      save_routine_timer = currentMillis;
      led_flash_timer = currentMillis;
      led_flash_color = 0;
    }

    if (!modeButtonPressed[0] && !modeButtonPressed[1]) {
      save_routine_flag = false;
      stuff_saved = false;
    }

    if (save_routine_flag) {
      if (currentMillis - led_flash_timer > LED_FLASH_INTERVAL) {
        led_flash_timer = currentMillis;
        for (int ch = 0; ch < 2; ch++) {
          for (int i = 0; i < 2; i++) digitalWrite(ledPin[ch][i], !bitRead(led_flash_color, i));
        }
        if (led_flash_color < 3) led_flash_color += 1;
        else led_flash_color = 0;
      }
      if (currentMillis - save_routine_timer > SAVE_DELAY) {
        led_blink = 5;
        led_flash_timer = currentMillis;
        save_routine_flag = false;
        stuff_saved = true;
        save_stuff();
      }
    }

    if (led_blink) {
      if (currentMillis - led_flash_timer > 125) {
        led_flash_timer = currentMillis;
        for (int ch = 0; ch < 2; ch++) {
          for (int i = 0; i < 2; i++) digitalWrite(ledPin[ch][i], led_blink % 2);
        }
        led_blink -= 1;
        if (!led_blink) {
          for (int ch = 0; ch < 2; ch++) {
            ratio_led_phase[ch] = 0;
            ratio_led_timer[ch] = currentMillis;
            set_mode_led(ch);
          }
        }
      }
    }

    if (!save_routine_flag && !led_blink) {
      for (int ch = 0; ch < 2; ch++) {
        if (Mode[ch] == RATIO_MODE && currentMillis - ratio_led_timer[ch] >= RATIO_LED_INTERVAL) {
          ratio_led_timer[ch] = currentMillis;
          ratio_led_phase[ch] = (ratio_led_phase[ch] + 1) & 3;
          set_mode_led(ch);
        }
      }
    }
  }
}
