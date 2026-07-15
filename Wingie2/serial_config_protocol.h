#ifndef WINGIE2_SERIAL_CONFIG_PROTOCOL_H
#define WINGIE2_SERIAL_CONFIG_PROTOCOL_H

#include <ctype.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config_profiles.h"

namespace wingie_serial {

static const size_t kMaxFrameBytes = 1024;

enum Operation {
  kOperationInvalid,
  kOperationHello,
  kOperationGet,
  kOperationSet,
  kOperationSave,
  kOperationReset,
  kOperationStatus,
  kOperationGetCave,
  kOperationSetCave,
  kOperationGetState,
  kOperationSetParam,
  kOperationSetRatioValue,
  kOperationSetCaveValue,
  kOperationSetCaveMute
};

enum ParseErrorCode {
  kParseOk,
  kParseInvalidJson,
  kParseUnsupportedVersion,
  kParseMissingField,
  kParseInvalidField,
  kParseUnknownOperation,
  kParseFrameTooLarge
};

struct ParseError {
  ParseErrorCode code;
  int index;
};

struct Request {
  uint8_t version;
  uint32_t id;
  Operation operation;
  bool hasExpectedRevision;
  uint32_t expectedRevision;
  char side[6];
  uint8_t bank;
  float ratios[wingie_config::kRatioCount];
  float frequencies[wingie_config::kRatioCount];
  bool mute[wingie_config::kRatioCount];
  char target[16];
  char name[32];
  uint8_t index;
  float value;
  bool muteValue;
};

inline void setError(ParseError &error, ParseErrorCode code, int index = -1) {
  error.code = code;
  error.index = index;
}

inline const char *skipWhitespace(const char *cursor) {
  while (*cursor && isspace(static_cast<unsigned char>(*cursor))) cursor++;
  return cursor;
}

inline const char *findField(const char *json, const char *field) {
  char pattern[40];
  const int length = snprintf(pattern, sizeof(pattern), "\"%s\"", field);
  if (length < 0 || static_cast<size_t>(length) >= sizeof(pattern)) return nullptr;
  const char *cursor = json;
  while ((cursor = strstr(cursor, pattern)) != nullptr) {
    const char *colon = skipWhitespace(cursor + length);
    if (*colon == ':') return skipWhitespace(colon + 1);
    cursor += length;
  }
  return nullptr;
}

inline bool parseUnsigned(const char *cursor, uint32_t &value, const char *end = nullptr) {
  if (!cursor || *cursor == '-' || !isdigit(static_cast<unsigned char>(*cursor))) return false;
  char *parsedEnd = nullptr;
  const unsigned long parsed = strtoul(cursor, &parsedEnd, 10);
  if (parsedEnd == cursor || parsed > UINT32_MAX) return false;
  if (end && parsedEnd > end) return false;
  const char terminator = *skipWhitespace(parsedEnd);
  if (terminator != '\0' && terminator != ',' && terminator != '}' && terminator != ']') return false;
  value = static_cast<uint32_t>(parsed);
  return true;
}

inline bool parseNumber(const char *cursor, float &value, const char **end = nullptr) {
  if (!cursor) return false;
  char *parsedEnd = nullptr;
  value = strtof(cursor, &parsedEnd);
  if (parsedEnd == cursor || !isfinite(value)) return false;
  const char terminator = *skipWhitespace(parsedEnd);
  if (terminator != '\0' && terminator != ',' && terminator != '}' && terminator != ']') return false;
  if (end) *end = parsedEnd;
  return true;
}

inline bool parseString(const char *cursor, char *value, size_t capacity) {
  if (!cursor || *cursor != '"' || capacity == 0) return false;
  cursor++;
  size_t index = 0;
  while (*cursor && *cursor != '"') {
    if (*cursor == '\\' || index + 1 >= capacity) return false;
    value[index++] = *cursor++;
  }
  if (*cursor != '"') return false;
  value[index] = '\0';
  return true;
}

inline bool parseBoolean(const char *cursor, bool &value, const char **end = nullptr) {
  if (!cursor) return false;
  if (strncmp(cursor, "true", 4) == 0) {
    value = true;
    const char *parsedEnd = cursor + 4;
    const char terminator = *skipWhitespace(parsedEnd);
    if (terminator != '\0' && terminator != ',' && terminator != '}' && terminator != ']') return false;
    if (end) *end = parsedEnd;
    return true;
  }
  if (strncmp(cursor, "false", 5) == 0) {
    value = false;
    const char *parsedEnd = cursor + 5;
    const char terminator = *skipWhitespace(parsedEnd);
    if (terminator != '\0' && terminator != ',' && terminator != '}' && terminator != ']') return false;
    if (end) *end = parsedEnd;
    return true;
  }
  return false;
}

inline bool parseFloatArray(const char *cursor, float *values, size_t capacity, size_t &count) {
  if (!cursor || *cursor != '[') return false;
  cursor = skipWhitespace(cursor + 1);
  count = 0;
  if (*cursor == ']') return true;
  while (*cursor) {
    if (count >= capacity || !parseNumber(cursor, values[count], &cursor)) return false;
    count++;
    cursor = skipWhitespace(cursor);
    if (*cursor == ']') return true;
    if (*cursor != ',') return false;
    cursor = skipWhitespace(cursor + 1);
  }
  return false;
}

inline bool parseFrequencyArray(const char *cursor, float *values, size_t capacity, size_t &count) {
  return parseFloatArray(cursor, values, capacity, count);
}

inline bool parseBooleanArray(const char *cursor, bool *values, size_t capacity, size_t &count) {
  if (!cursor || *cursor != '[') return false;
  cursor = skipWhitespace(cursor + 1);
  count = 0;
  if (*cursor == ']') return true;
  while (*cursor) {
    if (count >= capacity || !parseBoolean(cursor, values[count], &cursor)) return false;
    count++;
    cursor = skipWhitespace(cursor);
    if (*cursor == ']') return true;
    if (*cursor != ',') return false;
    cursor = skipWhitespace(cursor + 1);
  }
  return false;
}

inline Operation operationFromString(const char *value) {
  if (strcmp(value, "hello") == 0) return kOperationHello;
  if (strcmp(value, "get") == 0) return kOperationGet;
  if (strcmp(value, "set") == 0) return kOperationSet;
  if (strcmp(value, "save") == 0) return kOperationSave;
  if (strcmp(value, "reset") == 0) return kOperationReset;
  if (strcmp(value, "status") == 0) return kOperationStatus;
  if (strcmp(value, "get_cave") == 0) return kOperationGetCave;
  if (strcmp(value, "set_cave") == 0) return kOperationSetCave;
  if (strcmp(value, "get_state") == 0) return kOperationGetState;
  if (strcmp(value, "set_param") == 0) return kOperationSetParam;
  if (strcmp(value, "set_ratio_value") == 0) return kOperationSetRatioValue;
  if (strcmp(value, "set_cave_value") == 0) return kOperationSetCaveValue;
  if (strcmp(value, "set_cave_mute") == 0) return kOperationSetCaveMute;
  return kOperationInvalid;
}

inline bool parseRequestLine(const char *line, size_t length, Request &request, ParseError &error) {
  if (!line || length < 2 || length > kMaxFrameBytes || line[0] != '@') {
    setError(error, length > kMaxFrameBytes ? kParseFrameTooLarge : kParseInvalidJson);
    return false;
  }
  char json[kMaxFrameBytes + 1];
  memcpy(json, line + 1, length - 1);
  json[length - 1] = '\0';
  const char *object = skipWhitespace(json);
  if (*object != '{') {
    setError(error, kParseInvalidJson);
    return false;
  }

  memset(&request, 0, sizeof(request));
  uint32_t version = 0;
  const char *versionValue = findField(object, "v");
  const char *idValue = findField(object, "id");
  const char *operationValue = findField(object, "op");
  if (!versionValue || !idValue || !operationValue || !parseUnsigned(versionValue, version, nullptr)) {
    setError(error, kParseMissingField);
    return false;
  }
  if (version != 1) {
    setError(error, kParseUnsupportedVersion);
    return false;
  }
  if (!parseUnsigned(idValue, request.id, nullptr)) {
    setError(error, kParseInvalidField);
    return false;
  }
  char operationName[24];
  if (!parseString(operationValue, operationName, sizeof(operationName))) {
    setError(error, kParseInvalidField);
    return false;
  }
  request.version = static_cast<uint8_t>(version);
  request.operation = operationFromString(operationName);
  if (request.operation == kOperationInvalid) {
    setError(error, kParseUnknownOperation);
    return false;
  }

  const char *expectedRevision = findField(object, "expected_revision");
  if (expectedRevision) {
    if (!parseUnsigned(expectedRevision, request.expectedRevision, nullptr)) {
      setError(error, kParseInvalidField);
      return false;
    }
    request.hasExpectedRevision = true;
  }

  if (request.operation == kOperationSet) {
    const char *ratios = findField(object, "ratios");
    size_t count = 0;
    if (!ratios || !parseFloatArray(ratios, request.ratios, wingie_config::kRatioCount, count) || count != wingie_config::kRatioCount) {
      setError(error, kParseInvalidField);
      return false;
    }
  }

  if (request.operation == kOperationGetCave || request.operation == kOperationSetCave) {
    const char *side = findField(object, "side");
    const char *bank = findField(object, "bank");
    uint32_t parsedBank = 0;
    if (!side || !parseString(side, request.side, sizeof(request.side)) ||
        (strcmp(request.side, "left") != 0 && strcmp(request.side, "right") != 0) ||
        !bank || !parseUnsigned(bank, parsedBank, nullptr) || parsedBank >= wingie_config::kCaveBankCount) {
      setError(error, kParseInvalidField);
      return false;
    }
    request.bank = static_cast<uint8_t>(parsedBank);
  }

  if (request.operation == kOperationSetCave) {
    const char *frequencies = findField(object, "frequencies");
    const char *mute = findField(object, "mute");
    size_t frequencyCount = 0;
    size_t muteCount = 0;
    if (!frequencies || !mute ||
        !parseFrequencyArray(frequencies, request.frequencies, wingie_config::kRatioCount, frequencyCount) ||
        !parseBooleanArray(mute, request.mute, wingie_config::kRatioCount, muteCount) ||
        frequencyCount != wingie_config::kRatioCount || muteCount != wingie_config::kRatioCount) {
      setError(error, kParseInvalidField);
      return false;
    }
  }

  if (request.operation == kOperationSetParam) {
    const char *target = findField(object, "target");
    const char *name = findField(object, "name");
    const char *value = findField(object, "value");
    if (!target || !parseString(target, request.target, sizeof(request.target)) || request.target[0] == '\0' ||
        !name || !parseString(name, request.name, sizeof(request.name)) || request.name[0] == '\0' ||
        !value || !parseNumber(value, request.value)) {
      setError(error, kParseInvalidField);
      return false;
    }
  }

  if (request.operation == kOperationSetRatioValue || request.operation == kOperationSetCaveValue ||
      request.operation == kOperationSetCaveMute) {
    const char *index = findField(object, "index");
    uint32_t parsedIndex = 0;
    if (!index || !parseUnsigned(index, parsedIndex) || parsedIndex >= wingie_config::kRatioCount) {
      setError(error, kParseInvalidField);
      return false;
    }
    request.index = static_cast<uint8_t>(parsedIndex);
  }

  if (request.operation == kOperationSetRatioValue || request.operation == kOperationSetCaveValue) {
    const char *value = findField(object, "value");
    if (!value || !parseNumber(value, request.value)) {
      setError(error, kParseInvalidField);
      return false;
    }
  }

  if (request.operation == kOperationSetCaveValue || request.operation == kOperationSetCaveMute) {
    const char *target = findField(object, "target");
    const char *bank = findField(object, "bank");
    uint32_t parsedBank = 0;
    if (!target || !parseString(target, request.target, sizeof(request.target)) ||
        (strcmp(request.target, "left") != 0 && strcmp(request.target, "right") != 0) ||
        !bank || !parseUnsigned(bank, parsedBank) || parsedBank >= wingie_config::kCaveBankCount) {
      setError(error, kParseInvalidField);
      return false;
    }
    request.bank = static_cast<uint8_t>(parsedBank);
  }

  if (request.operation == kOperationSetCaveMute) {
    const char *mute = findField(object, "mute");
    if (!mute || !parseBoolean(mute, request.muteValue)) {
      setError(error, kParseInvalidField);
      return false;
    }
  }

  setError(error, kParseOk);
  return true;
}

}  // namespace wingie_serial

#endif
