#include <assert.h>
#include <string.h>

#include "../../Wingie2/serial_response.h"

using namespace wingie_serial;

static void checkResponse(const JsonResponse &response, const char *expected) {
  assert(response.valid);
  assert(response.length == strlen(expected));
  assert(strcmp(response.data, expected) == 0);
}

static void testHello() {
  JsonResponse response;
  encodeHello(response, 1, "dev");
  checkResponse(response,
      "{\"v\":1,\"id\":1,\"ok\":true,\"op\":\"hello\",\"device\":\"Wingie2\","
      "\"firmware\":\"dev\","
      "\"capabilities\":[\"settings\",\"ratio_mode\",\"cave_config\",\"mpe\"],"
      "\"config_schema\":6,\"transport\":{\"baud\":115200,\"max_frame\":1024}}");

  JsonResponse longFirmware;
  encodeHello(longFirmware, 2, "2.1.0-rc1");
  assert(strstr(longFirmware.data, "\"firmware\":\"2.1.0-rc1\"") != nullptr);
}

static void testError() {
  JsonResponse noField;
  encodeError(noField, 1, "invalid_ratio", nullptr, nullptr);
  checkResponse(noField,
      "{\"v\":1,\"id\":1,\"ok\":false,\"error\":{\"code\":\"invalid_ratio\"}}");

  JsonResponse withField;
  encodeError(withField, 2, "revision_conflict", "expected_revision", nullptr);
  checkResponse(withField,
      "{\"v\":1,\"id\":2,\"ok\":false,\"error\":{\"code\":\"revision_conflict\","
      "\"field\":\"expected_revision\"}}");

  JsonResponse withMessage;
  encodeError(withMessage, 3, "busy", nullptr, "configuration is still loading");
  checkResponse(withMessage,
      "{\"v\":1,\"id\":3,\"ok\":false,\"error\":{\"code\":\"busy\","
      "\"message\":\"configuration is still loading\"}}");

  JsonResponse withBoth;
  encodeError(withBoth, 4, "invalid_parameter", "mix", "value out of range");
  checkResponse(withBoth,
      "{\"v\":1,\"id\":4,\"ok\":false,\"error\":{\"code\":\"invalid_parameter\","
      "\"field\":\"mix\",\"message\":\"value out of range\"}}");
}

static void testCaveBank() {
  const float frequencies[wingie_config::kRatioCount] = {
    16.0f, 50.25f, 115.5f, 218.75f, 411.0f, 777.0f, 1500.0f, 5200.0f, 16000.0f
  };
  const bool mutes[wingie_config::kRatioCount] = {
    false, true, false, true, false, false, false, false, true
  };
  JsonResponse response;
  encodeCaveBank(response, 5, "left", 1, false, frequencies, mutes, 7, true);
  checkResponse(response,
      "{\"v\":1,\"id\":5,\"ok\":true,\"op\":\"get_cave\",\"side\":\"left\",\"bank\":1,"
      "\"active\":false,\"frequencies\":[16.00,50.25,115.50,218.75,411.00,777.00,1500.00,5200.00,16000.00],"
      "\"mute\":[false,true,false,true,false,false,false,false,true],"
      "\"revision\":7,\"dirty\":true,\"limits\":{\"min\":16.00,\"max\":16000.00,\"step\":0.01}}");

  JsonResponse activeResponse;
  encodeCaveBank(activeResponse, 6, "right", 2, true, frequencies, mutes, 0, false);
  assert(strstr(activeResponse.data, "\"side\":\"right\",\"bank\":2,\"active\":true") != nullptr);
  assert(strstr(activeResponse.data, "\"revision\":0,\"dirty\":false") != nullptr);
}

static void testSettings() {
  ChannelSettings left;
  left.mode = 1;
  left.mix = 0.5f;
  left.decay = 2.5f;
  left.volume = 0.75f;
  left.threshold = 0.4125f;

  ChannelSettings right;
  right.mode = 2;
  right.mix = 0.25f;
  right.decay = 1.0f;
  right.volume = 0.5f;
  right.threshold = 0.99f;

  SharedSettings shared;
  shared.a3Hz = 440.0f;
  shared.tuning = -1;
  shared.preClipGain = 0.2475f;
  shared.postClipGain = 0.825f;
  shared.midiLeft = 1;
  shared.midiRight = 2;
  shared.midiBoth = 3;
  shared.mpeEnabled = false;
  shared.lineInputMono = false;

  JsonResponse response;
  encodeSettings(response, 2, true, true, true, left, right, shared);
  checkResponse(response,
      "{\"v\":1,\"id\":2,\"ok\":true,\"op\":\"get_settings\",\"source\":\"line\",\"dirty\":true,"
      "\"dirty_all\":true,\"left\":{\"mode\":1,\"mix\":0.5000,\"decay\":2.5000,\"volume\":0.7500,\"threshold\":0.4125},"
      "\"right\":{\"mode\":2,\"mix\":0.2500,\"decay\":1.0000,\"volume\":0.5000,\"threshold\":0.9900},"
      "\"shared\":{\"a3_hz\":440.00,\"tuning\":-1,\"pre_clip_gain\":0.2475,\"post_clip_gain\":0.8250,"
      "\"midi\":{\"left\":1,\"right\":2,\"both\":3},\"mpe_enabled\":false,\"line_input_mono\":false},"
      "\"limits\":{\"mode\":[0,4,1],"
      "\"threshold\":[0.0825,0.99,0.0825],"
      "\"a3_hz\":[358.08,521.91,0.01],"
      "\"tuning\":[-1,7,1],"
      "\"pre_clip_gain\":[0.0825,0.99,0.0825],"
      "\"post_clip_gain\":[0.385,0.99,0.055],"
      "\"midi_left\":[1,16,1],\"midi_right\":[1,16,1],\"midi_both\":[1,16,1],"
      "\"mpe_enabled\":[0,1,1],\"line_input_mono\":[0,1,1]}}");

  shared.mpeEnabled = true;
  JsonResponse micResponse;
  encodeSettings(micResponse, 3, false, false, false, left, right, shared);
  assert(strstr(micResponse.data, "\"source\":\"mic\",\"dirty\":false") != nullptr);
  assert(strstr(micResponse.data, "\"dirty_all\":false") != nullptr);
  assert(strstr(micResponse.data, "\"mpe_enabled\":true,\"line_input_mono\":false},\"limits\":{") != nullptr);
}

static void testRatioProfile() {
  const float ratios[wingie_config::kRatioCount] = {
    0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 4.0f, 5.0f, 7.0f
  };
  JsonResponse response;
  encodeRatioProfile(response, 4, ratios, 7, true);
  checkResponse(response,
      "{\"v\":1,\"id\":4,\"ok\":true,\"op\":\"get\",\"profile\":{"
      "\"ratios\":[0.500,1.000,1.500,2.000,2.500,3.000,4.000,5.000,7.000],"
      "\"revision\":7,\"dirty\":true},"
      "\"factory_profile\":{\"ratios\":[1.000,2.000,3.000,4.000,5.000,6.000,7.000,8.000,9.000]},"
      "\"limits\":{\"min\":0.125,\"max\":32.000,\"step\":0.001,"
      "\"frequency_min\":16,\"frequency_max\":16000}}");
}

static void testQueued() {
  JsonResponse response;
  encodeQueued(response, 6, "set", 3);
  checkResponse(response,
      "{\"v\":1,\"id\":6,\"ok\":true,\"op\":\"set\",\"state\":\"queued\",\"revision\":3}");

  JsonResponse caveQueued;
  encodeQueued(caveQueued, 7, "set_cave", 12);
  checkResponse(caveQueued,
      "{\"v\":1,\"id\":7,\"ok\":true,\"op\":\"set_cave\",\"state\":\"queued\",\"revision\":12}");
}

static void testParameterResponse() {
  JsonResponse response;
  encodeParameterResponse(response, 8, 0.625f, false, true);
  checkResponse(response,
      "{\"v\":1,\"id\":8,\"ok\":true,\"op\":\"set_param\",\"value\":0.6250,"
      "\"dirty\":false,\"caves_changed\":true}");

  JsonResponse dirtyResponse;
  encodeParameterResponse(dirtyResponse, 9, 1.0f, true, false);
  checkResponse(dirtyResponse,
      "{\"v\":1,\"id\":9,\"ok\":true,\"op\":\"set_param\",\"value\":1.0000,"
      "\"dirty\":true,\"caves_changed\":false}");
}

static void testSaveOk() {
  JsonResponse response;
  encodeSaveOk(response, 10);
  checkResponse(response,
      "{\"v\":1,\"id\":10,\"ok\":true,\"op\":\"save\",\"state\":\"saved\"}");
}

static void testStatus() {
  StatusSnapshot snapshot;
  snapshot.mode[0] = 0;
  snapshot.mode[1] = 1;
  snapshot.note[0] = 60;
  snapshot.note[1] = 64;
  snapshot.fundamentalHz[0] = 261.6256f;
  snapshot.fundamentalHz[1] = 329.6276f;
  snapshot.midiRx = 42;
  snapshot.profileRevision = 3;
  snapshot.activeBank[0] = 1;
  snapshot.activeBank[1] = 2;
  snapshot.caveRevision[0][0] = 0;
  snapshot.caveRevision[0][1] = 1;
  snapshot.caveRevision[0][2] = 2;
  snapshot.caveRevision[1][0] = 3;
  snapshot.caveRevision[1][1] = 4;
  snapshot.caveRevision[1][2] = 5;

  JsonResponse response;
  encodeStatus(response, 9, snapshot);
  checkResponse(response,
      "{\"v\":1,\"id\":9,\"ok\":true,\"op\":\"status\","
      "\"mode\":{\"left\":0,\"right\":1},"
      "\"note\":{\"left\":60,\"right\":64},"
      "\"fundamental_hz\":{\"left\":261.626,\"right\":329.628},"
      "\"midi_rx\":42,"
      "\"profile_revision\":3,\"cave_active_bank\":{\"left\":1,\"right\":2},"
      "\"cave_revision\":{\"left\":[0,1,2],\"right\":[3,4,5]}}");
}

static void testControlCounts() {
  ControlCountsSnapshot snapshot;
  snapshot.midiRx = 11;
  snapshot.note[0] = 60;
  snapshot.note[1] = 61;
  for (uint8_t i = 0; i < 12; i++) {
    snapshot.key[0][i] = i;
    snapshot.key[1][i] = i + 10;
  }
  snapshot.modeButton[0] = 1;
  snapshot.modeButton[1] = 2;
  snapshot.octButton[0][0] = 3;
  snapshot.octButton[0][1] = 4;
  snapshot.octButton[1][0] = 5;
  snapshot.octButton[1][1] = 6;
  snapshot.sourceSwitch = 7;
  snapshot.pot[0] = 8;
  snapshot.pot[1] = 9;
  snapshot.pot[2] = 10;

  JsonResponse response;
  encodeControlCounts(response, 10, snapshot);
  checkResponse(response,
      "{\"v\":1,\"id\":10,\"ok\":true,\"op\":\"get_controls\","
      "\"midi_rx\":11,\"note\":{\"left\":60,\"right\":61},"
      "\"counts\":{\"key\":{\"left\":[0,1,2,3,4,5,6,7,8,9,10,11],"
      "\"right\":[10,11,12,13,14,15,16,17,18,19,20,21]},"
      "\"mode_button\":[1,2],"
      "\"oct_button\":{\"left\":[3,4],\"right\":[5,6]},"
      "\"source_switch\":7,\"pot\":[8,9,10]}}");
}

static void testResponseTooLargeFallback() {
  JsonResponse response;
  char huge[1100];
  memset(huge, 'x', sizeof(huge) - 1);
  huge[sizeof(huge) - 1] = '\0';
  response.append("%s", huge);
  assert(!response.valid);

  char out[80];
  const size_t length = encodeResponseTooLarge(out, sizeof(out));
  const char *expected = "{\"v\":1,\"id\":0,\"ok\":false,\"error\":{\"code\":\"response_too_large\"}}";
  assert(length == strlen(expected));
  assert(strcmp(out, expected) == 0);
  assert(length < sizeof(out));

  JsonResponse shortResponse;
  encodeHello(shortResponse, 1, "dev");
  assert(shortResponse.valid);
}

static void testOverflowInvalidatesMidEncode() {
  JsonResponse response;
  char huge[1100];
  memset(huge, 'y', sizeof(huge) - 1);
  huge[sizeof(huge) - 1] = '\0';
  response.append("prefix");
  response.append("%s", huge);
  assert(!response.valid);
  response.append("more");  // append 必须被 valid=false 短路
  assert(response.length < sizeof(response.data));
  assert(strncmp(response.data, "prefix", 6) == 0);  // 溢出帧按原语义丢弃，只保留部分写入
  assert(response.length < 6 + wingie_serial::kMaxFrameBytes);
}

int main() {
  testHello();
  testError();
  testCaveBank();
  testSettings();
  testRatioProfile();
  testQueued();
  testParameterResponse();
  testSaveOk();
  testStatus();
  testControlCounts();
  testResponseTooLargeFallback();
  testOverflowInvalidatesMidEncode();
  return 0;
}
