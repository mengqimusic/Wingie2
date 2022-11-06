// v1.1
// customizable cave (on device & MIDI) ✔
// 3 caves ✔
// adjustable pre / post clipper gain (on device) ✔
// adjustable global tuning (frequency of A3) (MIDI) ✔
// adjustable MIDI channels (MIDI) ✔
// mute resonators that hits 16kHz ✔
// retain the mode settings when power off ✔
// parameter change caused only by MIDI CC MSB ✔
// delayed prefs save for MIDI global settings 2022-04-28 --- changed
// prefs save by button combination 2022-05-01 ✔
// save cave mute status 2022-05-02 ✔

// mengqimusic.com

#include <I2Cdev.h>

#include <Preferences.h>
#define RW_MODE false
#define RO_MODE true

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"

#include "AC101.h"
#include <Adafruit_AW9523.h>
#include <Wire.h>
#include "Wingie2.h"
#include "WiFi.h"
#include <MIDI.h>

#define MODE_NUM 3 // +1
#define POLY_MODE 0
#define STRING_MODE 1
#define BAR_MODE 2
#define CAVE_MODE 3

#define BASE_NOTE 48
#define POLY_MODE_NOTE_ADD_L 12
#define POLY_MODE_NOTE_ADD_R 24
#define MIC_BOOST 0xAAC4 // 0xFFC4 = 48dB, 0x99C4 = 30dB, 0x88C4 = 0dB
#define DAC_VOL 0xA2A2 // 左右通道输出音量 A0 = 0dB, A2 = 1.5dB, A4 = 3dB

#define CAVE_LOWEST_FREQ 8
#define CAVE_HIGHEST_FREQ 15999

#define CC_MODE 0
#define CC_MIX 11
#define CC_DECAY 1
#define CC_VOL 7

#define CC_MIDI_CH_L 20
#define CC_MIDI_CH_R 21
#define CC_MIDI_CH_BOTH 22
#define CC_A3_FREQ_MSB 23
#define CC_A3_FREQ_LSB 55
#define CC_CAVE_FREQ_1_MSB 23
#define CC_CAVE_FREQ_1_LSB 55

#define MSB 0
#define LSB 1

#define MIX 0
#define DECAY 1
#define VOL 2
#define slider_movement_detect 256

struct MySettings : public midi::DefaultSettings
{
  static const unsigned SysExMaxSize = 16;
  static const bool UseRunningStatus = false;
  static const bool Use1ByteParsing = true;
  static const long BaudRate = 31250;
};


Wingie2 dsp(48000, 32);
AC101 ac;
Adafruit_AW9523 aw0; // left channel
Adafruit_AW9523 aw1; // right channel
MIDI_CREATE_CUSTOM_INSTANCE(HardwareSerial, Serial2, MIDI, MySettings);
TaskHandle_t controlCore;
TaskHandle_t low_priority_handle;

Preferences prefs;


//
// hardware pins
//

const int lOctPin[2] = {6, 5}; // on aw1
const int rOctPin[2] = {13, 12}; // on aw1
const int modePin[2] = {4, 7}; // on aw1
const int sourcePin = 14; // on aw1

const int intn[2] = {23, 5};
const int rstn[2] = {22, 19};

const int potPin[3] = {34, 39, 36}, ledPin[2][2] = {{14, 13}, {21, 15}}, ledColor[4] = {3, 2, 0, 1}; // 白 黄 红 紫
const int sources[2] = {0x1040, 0x0408}; // MIC, LINE

//
// volumes
//
int volume = 0;

volatile bool an[2] = {0, 0};
bool keyChanged = false;
bool source, key[2][12], keyPrev[2][12], firstPress[2] = {true, true};
bool sourceChanged = false, sourceChanged2 = false;
int note[2], octPrev[2], oct[2], Mode[2] = {POLY_MODE, POLY_MODE}, allKeys[2] = {0, 0}, currentPoly[2] = {0, 0};
bool modeButtonState[2], modeButtonPressed[2], modeChangingFromKeys[2] = {false, false}, modeChangingFromMIDI[2] = {false, false}, duck_env_triggered[2] = {false, false};

//
// for MIDI
//
bool realtime_value_valid[3] = {true, true, true}, polyFlip = false;
int potValRealtime[3], potValSampled[3], midi_ch_l, midi_ch_r, midi_ch_both;
float a3_freq;
int midiVal[2][3][2], cave_freq_midi_value[2][9][2], a3_freq_midi_value[2]; // Channel, Type, (MSB, LSB)

//
// cave mode
//
int cm_freq[2][3][9]; // channel, cave_number, voice
int cm_freq_prev[2][3][9] = {
  {
    {62, 115, 218, 411, 777, 1500, 2800, 5200, 11000},
    {205, 304, 370, 523, 540, 800, 913, 1568, 2400},
    {212, 425, 531, 637, 1168, 2017, 2336, 2654, 3693}
  },
  {
    {62, 115, 218, 411, 777, 1500, 2800, 5200, 11000},
    {205, 304, 370, 523, 540, 800, 913, 1568, 2400},
    {212, 425, 531, 637, 1062, 2017, 2336, 2654, 3693}
  }
};
bool cm_ms[2][3][9]; // mute states
bool cm_ms_prev[2][3][9] = {
  { {0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0}
  },
  {
    {0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0}
  }
};
bool cave_midi_set[2] = {false, false};

//
// global settings
//
float pre_clip_gain, post_clip_gain, left_thresh, right_thresh;
bool save_routine_flag = false, stuff_saved = false, dirty[10];
byte led_flash_color = 0, led_blink = 0;;
#define LED_FLASH_INTERVAL 250
#define SAVE_DELAY 3000
unsigned long save_routine_timer, led_flash_timer;

//
// for Tap Sequencer
//
bool trig[2] = {false, false}, trigged[2] = {false, false}, threshChanged[2] = {false, false};
int seq[2][12], seqLen[2] = {0, 0}, playHeadPos[2] = {0, 0}, writeHeadPos[2] = {0, 0};

unsigned long currentMillis, routineReadTimer = 0, sourceChangedMillis = 0, startupMillis = 0, duck_env_init_timer[2] = {0};

bool startup = true, core_print = true; // 启动淡入所用

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_MODE_NULL);
  btStop();

  MIDI.begin(MIDI_CHANNEL_OMNI);
  MIDI.setHandleNoteOn(handleNoteOn);
  MIDI.setHandleControlChange(handleControlChange);
  MIDI.MidiInterface::turnThruOff();

  Serial.print("setup running on core ");
  Serial.println(xPortGetCoreID());

  dsp.start();

  xTaskCreatePinnedToCore(
    control,        /* Task function. */
    "control",      /* name of task. */
    10000,          /* Stack size of task */
    NULL,           /* parameter of the task */
    1,              /* priority of the task */
    &controlCore,   /* Task handle to keep track of created task */
    1);             /* pin task to core 1 */
}

void loop() {
  if (core_print) {
    core_print = false;
    Serial.print("loop running on core ");
    Serial.println(xPortGetCoreID());
  }

  MIDI.read();
}

void Pressed0() {
  an[0] = 1;
}

void Pressed1() {
  an[1] = 1;
}

void cm_mute_set(byte kb, byte voice, bool state) {
  char buff[100];
  if (!kb) snprintf(buff, sizeof(buff), "/Wingie/left/mute_%d", voice);
  else snprintf(buff, sizeof(buff), "/Wingie/right/mute_%d", voice);
  const std::string str = buff;
  dsp.setParamValue(str, state);
}

void cm_freq_set(byte kb, byte voice, int freq) {
  char buff[100];
  if (!kb) snprintf(buff, sizeof(buff), "/Wingie/left/cave_freq_%d", voice);
  else snprintf(buff, sizeof(buff), "/Wingie/right/cave_freq_%d", voice);
  const std::string str = buff;
  dsp.setParamValue(str, freq);
}
