#include <I2Cdev.h>

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
#define INT_MODE 1
#define BAR_MODE 2
#define REQ_MODE 3

#define BASE_NOTE 48
#define POLY_MODE_NOTE_ADD_L 12
#define POLY_MODE_NOTE_ADD_R 24
#define MIC_BOOST 0xAAC4 // 0xFFC4 = 48dB, 0x99C4 = 30dB, 0x88C4 = 0dB
#define DAC_VOL 0xA2A2 // 左右通道输出音量 A0 = 0dB, A2 = 1.5dB, A4 = 3dB

#define MODE_CC 0
#define MIX_CC 11
#define DECAY_CC 1
#define VOL_CC 7
#define MSB 0
#define LSB 1
#define MIX 0
#define DECAY 1
#define VOL 2
#define slider_movement_detect 256

Wingie2 dsp(44100, 32);
AC101 ac;
Adafruit_AW9523 aw0; // left channel
Adafruit_AW9523 aw1; // right channel
MIDI_CREATE_INSTANCE(HardwareSerial, Serial2, MIDI);
TaskHandle_t controlCore;

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
bool muteStatus[2][9];
int note[2], octPrev[2], Mode[2] = {POLY_MODE, POLY_MODE}, allKeys[2] = {0, 0}, currentPoly[2] = {0, 0};
bool modeButtonState[2], modeButtonPressed[2], modeChangingFromKeys[2] = {false, false}, modeChangingFromMIDI[2] = {false, false}, modeChanged[2] = {false, false};

//
// for MIDI
//
bool midiValValid[3] = {false}, polyFlip = false;
int potValRealtime[3], potValSampled[3];
int midiVal[2][3][2]; // Channel, MSB, LSB

//
// for Tap Sequencer
//
bool trig[2] = {false, false}, trigged[2] = {false, false}, threshChanged[2] = {false, false};
int seq[2][12], seqLen[2] = {0, 0}, playHeadPos[2] = {0, 0}, writeHeadPos[2] = {0, 0};

unsigned long currentMillis, routineReadTimer = 0, sourceChangedMillis = 0, startupMillis = 0, modeChangedTimer[2] = {0};

bool startup = true; // 启动淡入所用

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_MODE_NULL);
  btStop();

  MIDI.begin(MIDI_CHANNEL_OMNI);
  MIDI.setHandleNoteOn(handleNoteOn);
  MIDI.setHandleControlChange(handleControlChange);

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
  MIDI.read();
}

void Pressed0() {
  an[0] = 1;
}

void Pressed1() {
  an[1] = 1;
}

void muteControl(byte kb, byte voice, bool state) {
  char buff[100];
  if (!kb) snprintf(buff, sizeof(buff), "/Wingie/left/mute_%d", voice);
  else snprintf(buff, sizeof(buff), "/Wingie/right/mute_%d", voice);
  const std::string str = buff;
  dsp.setParamValue(str, state);
}
