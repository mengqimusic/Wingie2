#!/bin/bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: build_benchmark.sh <generated-cpp>" >&2
  exit 2
fi
if [[ -z "${FAUST_ROOT:-}" ]]; then
  echo "FAUST_ROOT must point to Faust-2.59.6" >&2
  exit 2
fi

GENERATED_CPP=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
OUT=/tmp/wingie2-anti-feedback-benchmark
SKETCH="$OUT/sketch"
rm -rf "$OUT"
mkdir -p "$SKETCH" "$OUT/build"

python3 "$ROOT/tests/dsp/extract_faust_user.py" \
  "$GENERATED_CPP" "$SKETCH/generated_user.h"
cp "$ROOT/tests/esp32/wingie2_anti_feedback_benchmark/benchmark.cpp" \
  "$SKETCH/benchmark.cpp"
touch "$SKETCH/sketch.ino"

arduino-cli compile --fqbn esp32:esp32:esp32 \
  --build-property "compiler.cpp.extra_flags=-I$FAUST_ROOT/include" \
  --output-dir "$OUT/build" "$SKETCH"

echo "$OUT/build/sketch.ino.bin"
