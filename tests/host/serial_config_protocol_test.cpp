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
}

static void testRatioSet() {
  Request request = parse("@{\"v\":1,\"id\":4,\"op\":\"set\",\"expected_revision\":7,\"ratios\":[0.5,1,1.5,2,2.5,3,4,5,7]}");
  assert(request.operation == kOperationSet);
  assert(request.hasExpectedRevision);
  assert(request.expectedRevision == 7);
  assert(fabsf(request.ratios[0] - 0.5f) < 1e-6f);
  assert(fabsf(request.ratios[8] - 7.0f) < 1e-6f);
}

static void testCaveCommands() {
  Request get = parse("@{\"v\":1,\"id\":5,\"op\":\"get_cave\",\"side\":\"right\",\"bank\":2}");
  assert(get.operation == kOperationGetCave);
  assert(strcmp(get.side, "right") == 0);
  assert(get.bank == 2);

  Request set = parse("@{\"v\":1,\"id\":6,\"op\":\"set_cave\",\"side\":\"left\",\"bank\":0,\"frequencies\":[16.01,50.25,115.5,218.75,411,777,1500,5200,16000.00],\"mute\":[false,true,false,true,false,false,false,false,true]}");
  assert(set.operation == kOperationSetCave);
  assert(strcmp(set.side, "left") == 0);
  assert(set.bank == 0);
  assert(fabsf(set.frequencies[0] - 16.01f) < 1e-5f);
  assert(fabsf(set.frequencies[1] - 50.25f) < 1e-5f);
  assert(fabsf(set.frequencies[8] - 16000.0f) < 1e-3f);
  assert(!set.mute[0]);
  assert(set.mute[1]);
  assert(set.mute[8]);
  assert(wingie_config::validateCaveBank(set.frequencies, set.mute, wingie_config::kRatioCount));
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

  char oversized[kMaxFrameBytes + 2];
  memset(oversized, 'x', sizeof(oversized));
  oversized[0] = '@';
  assert(!parseRequestLine(oversized, sizeof(oversized), request, error));
  assert(error.code == kParseFrameTooLarge);
}

int main() {
  testSimpleCommands();
  testRatioSet();
  testCaveCommands();
  testInvalidRequests();
  return 0;
}
