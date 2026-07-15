#include <assert.h>
#include <math.h>
#include <string.h>

#include "../../Wingie2/serial_config_protocol.h"

using namespace wingie_serial;

static Request parse(const char *line) {
  Request request;
  ParseError error;
  assert(parseRequestLine(line, strlen(line), request, error));
  assert(error.code == kParseOk);
  return request;
}

static void testSimpleCommands() {
  Request hello = parse("@{\"v\":1,\"id\":1,\"op\":\"hello\"}");
  assert(hello.operation == kOperationHello);
  assert(hello.id == 1);

  Request save = parse("@{\"v\":1,\"id\":2,\"op\":\"save\"}");
  assert(save.operation == kOperationSave);

  Request status = parse("@{ \"v\": 1, \"id\": 3, \"op\": \"status\" }");
  assert(status.operation == kOperationStatus);

  Request state = parse("@{\"v\":1,\"id\":4,\"op\":\"get_state\"}");
  assert(state.operation == kOperationGetState);
}

static void testRatioSet() {
  Request request = parse("@{\"v\":1,\"id\":4,\"op\":\"set\",\"expected_revision\":7,\"ratios\":[0.5,1,1.5,2,2.5,3,4,5,7]}");
  assert(request.operation == kOperationSet);
  assert(request.hasExpectedRevision);
  assert(request.expectedRevision == 7);
  assert(fabsf(request.ratios[0] - 0.5f) < 1e-6f);
  assert(fabsf(request.ratios[8] - 7.0f) < 1e-6f);
}

static void testLegacyCaveCommands() {
  Request get = parse("@{\"v\":1,\"id\":5,\"op\":\"get_cave\",\"side\":\"right\",\"bank\":2}");
  assert(get.operation == kOperationGetCave);
  assert(strcmp(get.side, "right") == 0);
  assert(get.bank == 2);

  Request set = parse("@{\"v\":1,\"id\":6,\"op\":\"set_cave\",\"side\":\"left\",\"bank\":0,\"frequencies\":[16.01,50.25,115.5,218.75,411,777,1500,5200,15999.99],\"mute\":[false,true,false,true,false,false,false,false,true]}");
  assert(set.operation == kOperationSetCave);
  assert(strcmp(set.side, "left") == 0);
  assert(set.bank == 0);
  assert(fabsf(set.frequencies[0] - 16.01f) < 1e-5f);
  assert(fabsf(set.frequencies[8] - 15999.99f) < 1e-3f);
  assert(!set.mute[0]);
  assert(set.mute[1]);
  assert(set.mute[8]);
}

static void testRealtimeCommands() {
  Request parameter = parse("@{\"v\":1,\"id\":7,\"op\":\"set_param\",\"target\":\"left\",\"name\":\"mix\",\"value\":0.625}");
  assert(parameter.operation == kOperationSetParam);
  assert(strcmp(parameter.target, "left") == 0);
  assert(strcmp(parameter.name, "mix") == 0);
  assert(fabsf(parameter.value - 0.625f) < 1e-6f);

  Request ratio = parse("@{\"v\":1,\"id\":8,\"op\":\"set_ratio_value\",\"index\":3,\"value\":0.5006,\"expected_revision\":2}");
  assert(ratio.operation == kOperationSetRatioValue);
  assert(ratio.index == 3);
  assert(fabsf(ratio.value - 0.5006f) < 1e-6f);
  assert(ratio.hasExpectedRevision && ratio.expectedRevision == 2);

  Request caveValue = parse("@{\"v\":1,\"id\":9,\"op\":\"set_cave_value\",\"target\":\"right\",\"bank\":2,\"index\":8,\"value\":20000}");
  assert(caveValue.operation == kOperationSetCaveValue);
  assert(strcmp(caveValue.target, "right") == 0);
  assert(caveValue.bank == 2);
  assert(caveValue.index == 8);
  assert(caveValue.value == 20000.0f);

  Request caveMute = parse("@{\"v\":1,\"id\":10,\"op\":\"set_cave_mute\",\"target\":\"left\",\"bank\":1,\"index\":7,\"mute\":true}");
  assert(caveMute.operation == kOperationSetCaveMute);
  assert(strcmp(caveMute.target, "left") == 0);
  assert(caveMute.bank == 1);
  assert(caveMute.index == 7);
  assert(caveMute.muteValue);
}

static void testInvalidRequests() {
  Request request;
  ParseError error;
  const char *unsupportedVersion = "@{\"v\":2,\"id\":1,\"op\":\"get\"}";
  assert(!parseRequestLine(unsupportedVersion, strlen(unsupportedVersion), request, error));
  assert(error.code == kParseUnsupportedVersion);

  const char *unknown = "@{\"v\":1,\"id\":1,\"op\":\"unknown\"}";
  assert(!parseRequestLine(unknown, strlen(unknown), request, error));
  assert(error.code == kParseUnknownOperation);

  const char *shortRatios = "@{\"v\":1,\"id\":1,\"op\":\"set\",\"ratios\":[1,2]}";
  assert(!parseRequestLine(shortRatios, strlen(shortRatios), request, error));
  assert(error.code == kParseInvalidField);

  const char *badSide = "@{\"v\":1,\"id\":1,\"op\":\"get_cave\",\"side\":\"center\",\"bank\":0}";
  assert(!parseRequestLine(badSide, strlen(badSide), request, error));
  assert(error.code == kParseInvalidField);

  const char *badFrequency = "@{\"v\":1,\"id\":1,\"op\":\"set_cave\",\"side\":\"left\",\"bank\":0,\"frequencies\":[1],\"mute\":[false]}";
  assert(!parseRequestLine(badFrequency, strlen(badFrequency), request, error));
  assert(error.code == kParseInvalidField);

  const char *missingParamName = "@{\"v\":1,\"id\":1,\"op\":\"set_param\",\"target\":\"left\",\"value\":1}";
  assert(!parseRequestLine(missingParamName, strlen(missingParamName), request, error));
  assert(error.code == kParseInvalidField);

  const char *badRatioIndex = "@{\"v\":1,\"id\":1,\"op\":\"set_ratio_value\",\"index\":9,\"value\":1}";
  assert(!parseRequestLine(badRatioIndex, strlen(badRatioIndex), request, error));
  assert(error.code == kParseInvalidField);

  const char *missingCaveMute = "@{\"v\":1,\"id\":1,\"op\":\"set_cave_mute\",\"target\":\"left\",\"bank\":0,\"index\":0}";
  assert(!parseRequestLine(missingCaveMute, strlen(missingCaveMute), request, error));
  assert(error.code == kParseInvalidField);

  const char *invalidNumber = "@{\"v\":1,\"id\":1,\"op\":\"set_ratio_value\",\"index\":0,\"value\":1oops}";
  assert(!parseRequestLine(invalidNumber, strlen(invalidNumber), request, error));
  assert(error.code == kParseInvalidField);

  char oversized[kMaxFrameBytes + 2];
  memset(oversized, 'x', sizeof(oversized));
  oversized[0] = '@';
  assert(!parseRequestLine(oversized, sizeof(oversized), request, error));
  assert(error.code == kParseFrameTooLarge);
}

static void testExpandedFrameLimit() {
  char padded[700];
  const char *prefix = "@{\"v\":1,\"id\":11,\"op\":\"get_state\"";
  const size_t prefixLength = strlen(prefix);
  memcpy(padded, prefix, prefixLength);
  memset(padded + prefixLength, ' ', sizeof(padded) - prefixLength - 2);
  padded[sizeof(padded) - 2] = '}';
  padded[sizeof(padded) - 1] = '\0';
  Request request = parse(padded);
  assert(request.operation == kOperationGetState);
}

int main() {
  testSimpleCommands();
  testRatioSet();
  testLegacyCaveCommands();
  testRealtimeCommands();
  testInvalidRequests();
  testExpandedFrameLimit();
  return 0;
}
