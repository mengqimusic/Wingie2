void save_stuff() {
  //
  // save settings
  //
  prefs.begin("settings", RW_MODE);

  for (int ch = 0; ch < 2; ch++) {
    for (int cave = 0; cave < 3; cave++) {
      for (int v = 0; v < 9; v++) {
        if (cm_freq[ch][cave][v] != cm_freq_prev[ch][cave][v])  {
          cm_freq_prev[ch][cave][v] = cm_freq[ch][cave][v];

          char buff[100];
          if (!ch) snprintf(buff, sizeof(buff), "l_cf_%d_%d", cave, v);
          else snprintf(buff, sizeof(buff), "r_cf_%d_%d", cave, v);
          const char *addr = buff;
          if (prefs.putUShort(addr, cm_freq[ch][cave][v])) Serial.printf("ch %d cave %d voice %d frequency (%d) is saved.\n", ch, cave, v, cm_freq[ch][cave][v]);
        }

        if (cm_ms[ch][cave][v] != cm_ms_prev[ch][cave][v])  {
          cm_ms_prev[ch][cave][v] = cm_ms[ch][cave][v];

          char buff[100];
          if (!ch) snprintf(buff, sizeof(buff), "l_cms_%d_%d", cave, v);
          else snprintf(buff, sizeof(buff), "r_cms_%d_%d", cave, v);
          const char *addr = buff;
          if (prefs.putBool(addr, cm_ms[ch][cave][v])) Serial.printf("ch %d cave %d voice %d mute state (%d) is saved.\n", ch, cave, v, cm_ms[ch][cave][v]);
        }
      }
    }
  }

  if (dirty[0]) {
    dirty[0] = false;
    if (prefs.putUChar("midi_ch_l", midi_ch_l)) Serial.printf("midi_ch_l is saved, value is %d.\n", midi_ch_l);
  }
  if (dirty[1]) {
    dirty[1] = false;
    if (prefs.putUChar("midi_ch_r", midi_ch_r)) Serial.printf("midi_ch_r is saved, value is %d.\n", midi_ch_r);
  }
  if (dirty[2]) {
    dirty[2] = false;
    if (prefs.putUChar("midi_ch_both", midi_ch_both)) Serial.printf("midi_ch_both is saved, value is %d.\n", midi_ch_both);
  }

  if (dirty[3]) {
    dirty[3] = false;
    float freq_offset = a3_freq - 440.;
    if (prefs.putFloat("a3_freq_offset", freq_offset)) Serial.printf("a3_freq_offset (%.2fHz) is saved. a3 = %.2fHz.\n", freq_offset, a3_freq);
  }

  if (dirty[4]) {
    dirty[4] = false;
    if (prefs.putFloat("left_thresh", left_thresh)) Serial.printf("left_thresh is saved, value is %.4f\n", left_thresh);
  }
  if (dirty[5]) {
    dirty[5] = false;
    if (prefs.putFloat("right_thresh", right_thresh)) Serial.printf("right_thresh is saved, value is %.4f\n", right_thresh);
  }
  if (dirty[6]) {
    dirty[6] = false;
    if (prefs.putFloat("pre_clip_gain", pre_clip_gain)) Serial.printf("pre_clip_gain is saved, value is %.4f\n", pre_clip_gain);
  }
  if (dirty[7]) {
    dirty[7] = false;
    if (prefs.putFloat("post_clip_gain", post_clip_gain)) Serial.printf("post_clip_gain is saved, value is %.4f\n", post_clip_gain);
  }

  if (dirty[8]) {
    dirty[8] = false;
    if (prefs.putUChar("left_mode", Mode[0])) Serial.printf("left_mode is saved, value is %d.\n", Mode[0]);
  }
  if (dirty[9]) {
    dirty[9] = false;
    if (prefs.putUChar("right_mode", Mode[1])) Serial.printf("right_mode is saved, value is %d.\n", Mode[1]);
  }

  prefs.end();
}
