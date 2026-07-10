# TF2NP Mode Filter Stability Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Evaluate and, only if every gate passes, replace Wingie2's direct-form modal biquads with Faust 2.59.6 protected normalized-ladder `fi.tf2np` filters without changing MIDI/control behavior.

**Architecture:** The plan first proves exact Faust 2.59.6 generation parity, then builds isolated old/new static-response tests and an Xtensa `compute(32)` benchmark. The product DSP source and generated files are changed only after those tools exist; hardware stress and user A/B decide whether the candidate is accepted or restored.

**Tech Stack:** Faust 2.59.6, `filters.lib` `fi.tf2np`, C++11, Python 3 with NumPy/SciPy, ESP32 Arduino Core 2.0.4, Xtensa cycle counter, CoreMIDI, pyserial, esptool 3.3.

---

## File Structure

- Create: `tests/dsp/mode_filter_compare.dsp` - isolated direct-form and protected filter outputs.
- Create: `tests/dsp/render_mode_filter.cpp` - Faust host-render architecture.
- Create: `tests/dsp/analyze_mode_filter.py` - deterministic static metric matrix.
- Create: `tests/esp32/wingie2_dsp_benchmark/benchmark.cpp` - Faust Arduino benchmark architecture.
- Create: `tests/esp32/wingie2_dsp_benchmark/build_benchmark.sh` - generate and compile benchmark images.
- Create: `Tools/midi_diag_session.py` - persistent serial diagnostics required for valid stress snapshots.
- Modify: `Tools/midi_stress.swift` - deterministic mode/decay configuration command.
- Modify: `Wingie2.dsp` - define `stableModeFilter` and route `r()` through it.
- Regenerate together: `Wingie2/Wingie2.cpp`, `Wingie2/Wingie2.h`.
- Create after testing: `docs/superpowers/results/2026-07-10-mode-filter-tf2np-results.md`.

### Task 1: Pin Faust 2.59.6 and Prove Regeneration Parity

**Files:**
- Read: `Wingie2.dsp`
- Read: `Wingie2/Wingie2.cpp`
- Read: `Wingie2/Wingie2.h`
- Generate outside repository: `/tmp/wingie2-faust-parity/`

- [ ] **Step 1: Download and mount the official arm64 release**

Run:

```bash
curl -L --fail --silent --show-error \
  -o /tmp/Faust-2.59.6-arm64.dmg \
  https://github.com/grame-cncm/faust/releases/download/2.59.6/Faust-2.59.6-arm64.dmg
shasum -a 256 /tmp/Faust-2.59.6-arm64.dmg
rm -rf /tmp/faust-2.59.6-mount
mkdir -p /tmp/faust-2.59.6-mount
hdiutil attach -readonly -nobrowse \
  -mountpoint /tmp/faust-2.59.6-mount \
  /tmp/Faust-2.59.6-arm64.dmg
```

Expected SHA-256:

```text
55a52324615c04fcb23a7e32d5cea48c34647d87dab88e30edbed5ba7a22080c
```

- [ ] **Step 2: Verify compiler and the correct plural standard library**

Run:

```bash
export FAUST_ROOT=/tmp/faust-2.59.6-mount/Faust-2.59.6
export PATH="$FAUST_ROOT/bin:$PATH"
export FAUSTARCH="$FAUST_ROOT/share/faust"
faust --version
rg -n "smax = 1.0-ma.EPSILON" "$FAUST_ROOT/share/faust/filters.lib"
rg -n "smax = 0.9999" "$FAUST_ROOT/share/faust/filter.lib"
shasum -a 256 "$FAUST_ROOT/bin/faust" \
  "$FAUST_ROOT/share/faust/filters.lib"
```

Expected: compiler reports `FAUST Version 2.59.6`; plural `filters.lib` contains the epsilon projection used by `fi.tf2np`; singular deprecated `filter.lib` is identified but not used. The compiler SHA-256 is `464d9ba249bea4b3589ad0ba5c7e8bf266b4f7e649523e68a022f73a164032c2` and `filters.lib` is `6f82542b67778a5f46edc0fc29bf18b5b7d0337b43689f6f2dd07a08c6da45fa`.

- [ ] **Step 3: Generate the unchanged DSP in a temporary directory**

Run:

```bash
rm -rf /tmp/wingie2-faust-parity
mkdir -p /tmp/wingie2-faust-parity
cp Wingie2.dsp /tmp/wingie2-faust-parity/Wingie2.dsp
cd /tmp/wingie2-faust-parity
faust2esp32 -ac101 -lib Wingie2.dsp
cd /Users/mengwu/Documents/Code/Wingie2
diff -u Wingie2/Wingie2.cpp \
  /tmp/wingie2-faust-parity/Wingie2/Wingie2.cpp
diff -u Wingie2/Wingie2.h \
  /tmp/wingie2-faust-parity/Wingie2/Wingie2.h
```

Expected: both diffs are empty. If either diff contains executable or architecture changes, stop this plan and resolve the Faust installation before editing DSP source.

- [ ] **Step 4: Compile the parity output against Core 2.0.4**

Run:

```bash
arduino-cli board details --fqbn esp32:esp32:esp32 | \
  rg 'Board version:.*2\.0\.4'
rm -rf /tmp/wingie2-parity-repo /tmp/wingie2-parity-build
mkdir -p /tmp/wingie2-parity-repo
rsync -a --exclude .git ./ /tmp/wingie2-parity-repo/
cp /tmp/wingie2-faust-parity/Wingie2/Wingie2.cpp \
  /tmp/wingie2-parity-repo/Wingie2/Wingie2.cpp
cp /tmp/wingie2-faust-parity/Wingie2/Wingie2.h \
  /tmp/wingie2-parity-repo/Wingie2/Wingie2.h
arduino-cli compile --fqbn esp32:esp32:esp32 \
  --libraries /tmp/wingie2-parity-repo/Libraries \
  --output-dir /tmp/wingie2-parity-build \
  /tmp/wingie2-parity-repo/Wingie2
```

Expected: board details resolves to Core 2.0.4 (the local mirror package may display `2.0.4-cn`); compile exits 0 and the program image remains below 1,310,720 bytes.

### Task 2: Build the Static Old/New DSP Comparison

**Files:**
- Create: `tests/dsp/mode_filter_compare.dsp`
- Create: `tests/dsp/render_mode_filter.cpp`
- Create: `tests/dsp/analyze_mode_filter.py`

- [ ] **Step 1: Add the isolated Faust comparison DSP**

Create `tests/dsp/mode_filter_compare.dsp`:

```faust
import("stdfaust.lib");

freq = hslider("freq", 440, 20, 15999, 0.01);
t60 = hslider("t60", 5, 0.1, 10, 0.01);
gain = hslider("gain", 1, 0, 1, 0.001);

w = 2 * ma.PI * freq / ma.SR;
rho = pow(0.001, 1.0 / (t60 * ma.SR));
a1 = -2 * rho * cos(w);
a2 = rho * rho;

direct = fi.tf2(1, 0, -1, a1, a2) * gain;
protected = fi.tf2np(1, 0, -1, a1, a2) * gain;

process = _ <: direct, protected;
```

- [ ] **Step 2: Add the deterministic host-render architecture**

Create `tests/dsp/render_mode_filter.cpp`:

```cpp
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "faust/dsp/dsp.h"
#include "faust/gui/MapUI.h"

<<includeIntrinsic>>
<<includeclass>>

static float deterministicNoise(uint32_t& state) {
  state = state * 1664525u + 1013904223u;
  return (float(int32_t(state)) / 2147483648.0f) * 0.05f;
}

int main(int argc, char** argv) {
  if (argc != 7) {
    std::cerr << "usage: renderer <freq> <t60> <gain> <seconds> "
                 "<impulse|noise|sine> <output.f32>\n";
    return 2;
  }

  constexpr int sampleRate = 44100;
  constexpr int blockSize = 32;
  const float frequency = std::strtof(argv[1], nullptr);
  const float decay = std::strtof(argv[2], nullptr);
  const float gain = std::strtof(argv[3], nullptr);
  const float seconds = std::strtof(argv[4], nullptr);
  const std::string source = argv[5];
  const int sampleCount = int(seconds * sampleRate);

  mydsp processor;
  processor.init(sampleRate);
  MapUI ui;
  processor.buildUserInterface(&ui);
  ui.setParamValue("freq", frequency);
  ui.setParamValue("t60", decay);
  ui.setParamValue("gain", gain);

  float input[blockSize] = {};
  float direct[blockSize] = {};
  float protectedOutput[blockSize] = {};
  float* inputs[] = {input};
  float* outputs[] = {direct, protectedOutput};
  std::ofstream stream(argv[6], std::ios::binary);
  if (!stream) return 3;

  uint32_t randomState = 0x57494e47u;
  double phase = 0.0;
  int rendered = 0;
  while (rendered < sampleCount) {
    const int frames = std::min(blockSize, sampleCount - rendered);
    for (int frame = 0; frame < blockSize; frame++) {
      if (frame >= frames) {
        input[frame] = 0.0f;
      } else if (source == "impulse") {
        input[frame] = (rendered + frame == 0) ? 1.0f : 0.0f;
      } else if (source == "noise") {
        input[frame] = deterministicNoise(randomState);
      } else if (source == "sine") {
        input[frame] = 0.05f * std::sin(phase);
        phase += 2.0 * 3.14159265358979323846 * 173.0 / sampleRate;
      } else {
        return 4;
      }
    }

    processor.compute(blockSize, inputs, outputs);
    for (int frame = 0; frame < frames; frame++) {
      stream.write(reinterpret_cast<const char*>(&direct[frame]), sizeof(float));
      stream.write(reinterpret_cast<const char*>(&protectedOutput[frame]), sizeof(float));
    }
    rendered += frames;
  }
  return stream.good() ? 0 : 5;
}
```

- [ ] **Step 3: Add the static metric analyzer**

Create `tests/dsp/analyze_mode_filter.py`:

```python
#!/usr/bin/env python3
import argparse
import json
import math
import pathlib
import subprocess
import tempfile

import numpy as np
from scipy.signal import hilbert


SAMPLE_RATE = 44100
NOTES = (24, 36, 60, 84, 96)
INDICES = (0, 4, 8)
DECAYS = (0.1, 5.0, 10.0)
CENTAU_RATIOS = (
    1.0, 21 / 20, 9 / 8, 7 / 6, 5 / 4, 4 / 3,
    7 / 5, 3 / 2, 14 / 9, 5 / 3, 7 / 4, 15 / 8,
)


def mtof(note, alternate):
    if not alternate:
        return 440.0 * 2.0 ** ((note - 69) / 12.0)
    degree = note % 12
    c_note = note - degree
    return 440.0 * 2.0 ** ((c_note - 69) / 12.0) * CENTAU_RATIOS[degree]


def mode_frequency(mode, note, index, alternate):
    base = mtof(note, alternate)
    if mode == "poly":
        return base * (index % 3 + 1)
    if mode == "string":
        return base * (index + 1)
    return base * 0.44444 * (index + 1.5) ** 2


def estimate_frequency(samples):
    analytic = hilbert(samples)
    envelope = np.abs(analytic)
    peak = float(np.max(envelope))
    times = np.arange(samples.size) / SAMPLE_RATE
    mask = (times > 0.005) & (envelope > peak * 1e-4)
    if np.count_nonzero(mask) < 32:
        raise ValueError("not enough samples for frequency estimate")
    phase = np.unwrap(np.angle(analytic[mask]))
    slope = np.polyfit(times[mask], phase, 1)[0]
    return abs(float(slope)) / (2.0 * math.pi)


def estimate_t60(samples):
    envelope = np.abs(hilbert(samples))
    peak = float(np.max(envelope))
    db = 20.0 * np.log10(np.maximum(envelope / peak, 1e-12))
    times = np.arange(samples.size) / SAMPLE_RATE
    mask = (times > 0.005) & (db < -3.0) & (db > -50.0)
    if np.count_nonzero(mask) < 32:
        raise ValueError("not enough samples for T60 estimate")
    slope = np.polyfit(times[mask], db[mask], 1)[0]
    if slope >= 0:
        raise ValueError("non-decaying envelope")
    return -60.0 / float(slope)


def render(renderer, frequency, decay, source, output):
    duration = max(0.5, min(12.0, decay * 1.2)) if source == "impulse" else 2.0
    subprocess.run(
        [renderer, str(frequency), str(decay), "1", str(duration), source, output],
        check=True,
    )
    data = np.fromfile(output, dtype=np.float32)
    if data.size % 2:
        raise ValueError("odd interleaved sample count")
    return data.reshape((-1, 2))


def analyze_pair(data, target_frequency, requested_t60):
    old = data[:, 0].astype(np.float64)
    new = data[:, 1].astype(np.float64)
    if not np.isfinite(old).all() or not np.isfinite(new).all():
        raise ValueError("non-finite output")

    old_frequency = estimate_frequency(old)
    new_frequency = estimate_frequency(new)
    cents = abs(1200.0 * math.log2(new_frequency / old_frequency))
    old_t60 = estimate_t60(old)
    new_t60 = estimate_t60(new)
    t60_error = abs(new_t60 - old_t60) / old_t60
    old_peak = float(np.max(np.abs(np.fft.rfft(old))))
    new_peak = float(np.max(np.abs(np.fft.rfft(new))))
    gain_db = abs(20.0 * math.log10(new_peak / old_peak))

    old_tail = old[-min(4096, old.size):]
    new_tail = new[-min(4096, new.size):]
    old_constant_dc = (
        float(np.std(old_tail)) < 1e-10
        and abs(float(np.mean(old_tail))) > 1e-7
    )
    new_constant_dc = (
        float(np.std(new_tail)) < 1e-10
        and abs(float(np.mean(new_tail))) > 1e-7
    )
    result = {
        "target_frequency": target_frequency,
        "requested_t60": requested_t60,
        "old_frequency": old_frequency,
        "new_frequency": new_frequency,
        "frequency_error_cents": cents,
        "old_t60": old_t60,
        "new_t60": new_t60,
        "t60_relative_error": t60_error,
        "gain_error_db": gain_db,
        "old_constant_dc": old_constant_dc,
        "new_constant_dc": new_constant_dc,
    }
    result["pass"] = (
        cents <= 1.0
        and t60_error <= 0.02
        and gain_db <= 0.25
        and not old_constant_dc
        and not new_constant_dc
    )
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("renderer")
    parser.add_argument("output_json")
    args = parser.parse_args()
    results = []

    with tempfile.TemporaryDirectory(prefix="wingie2-mode-filter-") as directory:
        raw_path = str(pathlib.Path(directory) / "render.f32")
        for alternate in (False, True):
            for mode in ("poly", "string", "bar"):
                for note in NOTES:
                    for index in INDICES:
                        frequency = mode_frequency(mode, note, index, alternate)
                        if frequency >= 16000.0:
                            continue
                        for decay in DECAYS:
                            data = render(args.renderer, frequency, decay, "impulse", raw_path)
                            metric = analyze_pair(data, frequency, decay)
                            metric.update({
                                "alternate": alternate,
                                "mode": mode,
                                "note": note,
                                "index": index,
                            })
                            results.append(metric)

        for source in ("noise", "sine"):
            data = render(args.renderer, 440.0, 10.0, source, raw_path)
            finite = bool(np.isfinite(data).all())
            active = bool(np.std(data[:, 1]) > 1e-8)
            results.append({"source": source, "finite": finite, "active": active,
                            "pass": finite and active})

    pathlib.Path(args.output_json).write_text(
        json.dumps(results, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    failures = [result for result in results if not result["pass"]]
    print(f"cases={len(results)} failures={len(failures)}")
    if failures:
        print(json.dumps(failures[:10], indent=2, sort_keys=True))
        raise SystemExit(1)


if __name__ == "__main__":
    main()
```

- [ ] **Step 4: Generate, compile, and run the comparison**

Run:

```bash
rm -rf /tmp/wingie2-dsp-test-venv
python3 -m venv /tmp/wingie2-dsp-test-venv
/tmp/wingie2-dsp-test-venv/bin/pip install \
  numpy==2.4.2 scipy==1.17.1
export FAUST_ROOT=/tmp/faust-2.59.6-mount/Faust-2.59.6
export PATH="$FAUST_ROOT/bin:$PATH"
faust -a tests/dsp/render_mode_filter.cpp -single -ftz 0 \
  tests/dsp/mode_filter_compare.dsp -o /tmp/mode_filter_renderer.cpp
clang++ -std=c++11 -O2 -I"$FAUST_ROOT/include" \
  /tmp/mode_filter_renderer.cpp -o /tmp/mode_filter_renderer
/tmp/wingie2-dsp-test-venv/bin/python \
  tests/dsp/analyze_mode_filter.py \
  /tmp/mode_filter_renderer /tmp/mode_filter_metrics.json
```

Expected: analyzer prints a nonzero case count and `failures=0`. If metric extraction itself fails, fix the test before changing product DSP; do not loosen the approved tolerances.

- [ ] **Step 5: Commit static comparison tools**

```bash
git add tests/dsp
git commit -m "test: 添加 mode filter 静态对照"
```

### Task 3: Add Persistent Stress Controls

**Files:**
- Create: `Tools/midi_diag_session.py`
- Modify: `Tools/midi_stress.swift`

- [ ] **Step 1: Add the persistent serial session tool**

Create `Tools/midi_diag_session.py`:

```python
#!/usr/bin/env python3
import argparse
import select
import sys

import serial


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/cu.usbserial-11310")
    args = parser.parse_args()
    connection = serial.Serial()
    connection.port = args.port
    connection.baudrate = 115200
    connection.timeout = 0
    connection.dtr = False
    connection.rts = False
    connection.open()
    print("HOST_SERIAL_SESSION_OPEN", flush=True)
    try:
        while True:
            readable, _, _ = select.select(
                [sys.stdin.fileno(), connection.fileno()], [], [], 0.1
            )
            if connection.fileno() in readable:
                waiting = connection.in_waiting
                chunk = connection.read(waiting if waiting else 1)
                if chunk:
                    sys.stdout.buffer.write(chunk)
                    sys.stdout.buffer.flush()
            if sys.stdin.fileno() in readable:
                line = sys.stdin.readline()
                if not line or line.strip() == "quit":
                    break
                command = line.strip()
                if command in ("r", "p"):
                    print(f"HOST_SERIAL_COMMAND {command}", flush=True)
                    connection.write(command.encode("ascii"))
    finally:
        connection.close()
        print("HOST_SERIAL_SESSION_CLOSED", flush=True)


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Extend CoreMIDI stimulus with deterministic configuration**

In `Tools/midi_stress.swift`, extend usage to:

```swift
private func usage() -> Never {
    FileHandle.standardError.write(Data(
        "usage: midi_stress.swift batch <channel> <pairs> <interval-us> | " +
        "marker <channel> <pitch> | config <channel> <mode-0-2> <decay-0-16383>\n".utf8
    ))
    exit(2)
}
```

Change the top-level argument count guard to:

```swift
guard arguments.count == 3 || arguments.count == 4 else { usage() }
```

Add this switch case before `default`:

```swift
case "config":
    guard arguments.count == 4,
          let modeValue = Int(arguments[2]), (0...2).contains(modeValue),
          let decayValue = Int(arguments[3]), (0...16383).contains(decayValue)
    else { usage() }
    let controlChange = UInt8(0xB0 | (channel - 1))
    send([controlChange, 0, UInt8(modeValue << 5)])
    send([controlChange, 33, UInt8(decayValue & 0x7F)])
    send([controlChange, 1, UInt8((decayValue >> 7) & 0x7F)])
    usleep(100_000)
    print("configured channel=\(channel) mode=\(modeValue) decay=\(decayValue)")
```

- [ ] **Step 3: Verify and commit diagnostic tools**

Run:

```bash
rm -rf /tmp/wingie2-midi-tools-venv
python3 -m venv /tmp/wingie2-midi-tools-venv
/tmp/wingie2-midi-tools-venv/bin/pip install pyserial==3.5
/tmp/wingie2-midi-tools-venv/bin/python \
  -m py_compile Tools/midi_diag_session.py
swiftc -typecheck Tools/midi_stress.swift
swift Tools/midi_stress.swift --help 2>&1 | rg "config"
```

Expected: Python and Swift checks exit 0; help output contains the config form.

Then commit:

```bash
rm -rf Tools/__pycache__
git add Tools/midi_diag_session.py Tools/midi_stress.swift
git commit -m "test: 添加持续 MIDI 压力配置"
```

### Task 4: Build the ESP32 Compute Benchmark

**Files:**
- Create: `tests/esp32/wingie2_dsp_benchmark/benchmark.cpp`
- Create: `tests/esp32/wingie2_dsp_benchmark/build_benchmark.sh`

- [ ] **Step 1: Add the Faust Arduino benchmark architecture**

Create `tests/esp32/wingie2_dsp_benchmark/benchmark.cpp`:

```cpp
#include <Arduino.h>
#include <stdint.h>
#include <xtensa/hal.h>

#include "faust/dsp/dsp.h"
#include "faust/gui/MapUI.h"

<<includeIntrinsic>>
<<includeclass>>

namespace {
constexpr int kSampleRate = 44100;
constexpr int kBlockSize = 32;
constexpr uint32_t kWarmupBlocks = 1000;
constexpr uint32_t kMeasuredBlocks = 100000;
constexpr uint16_t kHistogramBins = 8192;

mydsp processor;
MapUI ui;
float inputLeft[kBlockSize];
float inputRight[kBlockSize];
float outputLeft[kBlockSize];
float outputRight[kBlockSize];
float* inputs[] = {inputLeft, inputRight};
float* outputs[] = {outputLeft, outputRight};
uint32_t histogram[kHistogramBins] = {};
uint32_t randomState = 0x57494e47u;
uint32_t maxCycles = 0;
uint32_t deadlineMisses = 0;

float noise() {
  randomState = randomState * 1664525u + 1013904223u;
  return (float(int32_t(randomState)) / 2147483648.0f) * 0.05f;
}

void fillInput() {
  for (int index = 0; index < kBlockSize; index++) {
    inputLeft[index] = noise();
    inputRight[index] = noise();
  }
}

void setPitch(uint32_t block) {
  const float pitch = 36.0f + float(block % 60);
  ui.setParamValue("note0", pitch);
  ui.setParamValue("note1", pitch);
}

uint32_t percentile(uint32_t numerator, uint32_t denominator) {
  const uint32_t target = (kMeasuredBlocks * numerator + denominator - 1) / denominator;
  uint32_t accumulated = 0;
  for (uint32_t bin = 0; bin < kHistogramBins; bin++) {
    accumulated += histogram[bin];
    if (accumulated >= target) return bin;
  }
  return kHistogramBins - 1;
}
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  processor.init(kSampleRate);
  processor.buildUserInterface(&ui);
  ui.setParamValue("mode0", 1.0f);
  ui.setParamValue("mode1", 1.0f);
  ui.setParamValue("/Wingie/left/decay", 10.0f);
  ui.setParamValue("/Wingie/right/decay", 10.0f);

  for (uint32_t block = 0; block < kWarmupBlocks; block++) {
    setPitch(block);
    fillInput();
    processor.compute(kBlockSize, inputs, outputs);
  }

  const uint32_t cpuMHz = getCpuFrequencyMhz();
  const uint64_t deadlineCycles =
      uint64_t(cpuMHz) * 1000000ULL * kBlockSize / kSampleRate;
  for (uint32_t block = 0; block < kMeasuredBlocks; block++) {
    setPitch(block);
    fillInput();
    const uint32_t started = xthal_get_ccount();
    processor.compute(kBlockSize, inputs, outputs);
    const uint32_t cycles = xthal_get_ccount() - started;
    if (cycles > maxCycles) maxCycles = cycles;
    if (cycles >= deadlineCycles) deadlineMisses++;
    const uint32_t tenthsOfMicrosecond = uint32_t(
        (uint64_t(cycles) * 10 + cpuMHz - 1) / cpuMHz);
    const uint32_t histogramBin = tenthsOfMicrosecond < kHistogramBins
        ? tenthsOfMicrosecond : kHistogramBins - 1;
    histogram[histogramBin]++;
    if ((block & 255u) == 0) delay(1);
  }

  Serial.printf(
      "DSP_BENCH blocks=%lu cpu_mhz=%lu median_us=%.1f p95_us=%.1f "
      "p99_us=%.1f max_us=%.3f deadline_misses=%lu\n",
      (unsigned long)kMeasuredBlocks,
      (unsigned long)cpuMHz,
      double(percentile(50, 100)) / 10.0,
      double(percentile(95, 100)) / 10.0,
      double(percentile(99, 100)) / 10.0,
      double(maxCycles) / cpuMHz,
      (unsigned long)deadlineMisses);
}

void loop() {
  delay(1000);
}
```

- [ ] **Step 2: Add the benchmark build script**

Create `tests/esp32/wingie2_dsp_benchmark/build_benchmark.sh`:

```bash
#!/bin/bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: build_benchmark.sh <dsp-file> <label>" >&2
  exit 2
fi
if [[ -z "${FAUST_ROOT:-}" ]]; then
  echo "FAUST_ROOT must point to Faust-2.59.6" >&2
  exit 2
fi

DSP_FILE=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
LABEL=$2
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
OUT="/tmp/wingie2-dsp-benchmark-$LABEL"
SKETCH="$OUT/wingie2_dsp_benchmark"
rm -rf "$OUT"
mkdir -p "$SKETCH" "$OUT/build"

"$FAUST_ROOT/bin/faust" \
  -a "$ROOT/tests/esp32/wingie2_dsp_benchmark/benchmark.cpp" \
  -single -ftz 0 "$DSP_FILE" \
  -o "$SKETCH/wingie2_dsp_benchmark.cpp"
touch "$SKETCH/wingie2_dsp_benchmark.ino"

arduino-cli compile --fqbn esp32:esp32:esp32 \
  --build-property "compiler.cpp.extra_flags=-I$FAUST_ROOT/include" \
  --output-dir "$OUT/build" "$SKETCH"

echo "$OUT/build/wingie2_dsp_benchmark.ino.bin"
```

- [ ] **Step 3: Verify scripts and commit benchmark infrastructure**

Run:

```bash
chmod +x tests/esp32/wingie2_dsp_benchmark/build_benchmark.sh
export FAUST_ROOT=/tmp/faust-2.59.6-mount/Faust-2.59.6
arduino-cli board details --fqbn esp32:esp32:esp32 | \
  rg 'Board version:.*2\.0\.4'
tests/esp32/wingie2_dsp_benchmark/build_benchmark.sh Wingie2.dsp baseline
```

Expected: board details resolves to Core 2.0.4 (the local mirror package may display `2.0.4-cn`); compile exits 0 and prints `/tmp/wingie2-dsp-benchmark-baseline/build/wingie2_dsp_benchmark.ino.bin`.

Then commit:

```bash
git add tests/esp32
git commit -m "test: 添加 ESP32 DSP deadline benchmark"
```

### Task 5: Create and Measure the TF2NP Candidate

**Files:**
- Modify: `Wingie2.dsp:177`
- Generate outside repository: `/tmp/wingie2-tf2np-generated/`
- Write hardware: app0 at `0x10000` only

- [ ] **Step 1: Preserve the baseline DSP source and app0**

Run:

```bash
cp Wingie2.dsp /tmp/Wingie2-baseline.dsp
python3 -m venv /tmp/wingie2-esptool-venv
/tmp/wingie2-esptool-venv/bin/pip install esptool==3.3
/tmp/wingie2-esptool-venv/bin/esptool.py --chip esp32 \
  --port /dev/cu.usbserial-11310 read_flash \
  0x10000 0x140000 /tmp/wingie2-before-tf2np-app0.bin
/tmp/wingie2-esptool-venv/bin/esptool.py --chip esp32 \
  image_info /tmp/wingie2-before-tf2np-app0.bin
shasum -a 256 /tmp/Wingie2-baseline.dsp /tmp/wingie2-before-tf2np-app0.bin
```

Expected: app0 is 1,310,720 bytes with valid checksum/hash.

- [ ] **Step 2: Make the single DSP source change**

Add immediately before the existing `r(note, index, source)` definition:

```faust
stableModeFilter(freq, t60, gain) = fi.tf2np(1, 0, -1, a1, a2) * gain
with {
  w = 2 * ma.PI * freq / ma.SR;
  rho = pow(0.001, 1.0 / (t60 * ma.SR));
  a1 = -2 * rho * cos(w);
  a2 = rho * rho;
};
```

Replace:

```faust
r(note, index, source) = pm.modeFilter(a, b, ba.lin2LogGain(c))
```

with:

```faust
r(note, index, source) = stableModeFilter(a, b, ba.lin2LogGain(c))
```

- [ ] **Step 3: Re-run static tests against the product formula**

Run:

```bash
export FAUST_ROOT=/tmp/faust-2.59.6-mount/Faust-2.59.6
/tmp/wingie2-dsp-test-venv/bin/python \
  tests/dsp/analyze_mode_filter.py \
  /tmp/mode_filter_renderer /tmp/mode_filter_metrics.json
```

Expected: `failures=0`. This reconfirms the approved direct-form/`fi.tf2np` reference formula; Task 6's source and generated diff review proves the product source uses that exact formula. Neither check replaces hardware tests.

- [ ] **Step 4: Build baseline and candidate benchmark images**

Run:

```bash
tests/esp32/wingie2_dsp_benchmark/build_benchmark.sh \
  /tmp/Wingie2-baseline.dsp baseline
tests/esp32/wingie2_dsp_benchmark/build_benchmark.sh \
  Wingie2.dsp tf2np
shasum -a 256 \
  /tmp/wingie2-dsp-benchmark-baseline/build/wingie2_dsp_benchmark.ino.bin \
  /tmp/wingie2-dsp-benchmark-tf2np/build/wingie2_dsp_benchmark.ino.bin
```

Expected: both build successfully and fit app0.

- [ ] **Step 5: Flash and capture each benchmark**

For baseline, then candidate, run the same sequence with the corresponding image path:

```bash
/tmp/wingie2-esptool-venv/bin/esptool.py --chip esp32 \
  --port /dev/cu.usbserial-11310 --baud 115200 write_flash \
  0x10000 /tmp/wingie2-dsp-benchmark-baseline/build/wingie2_dsp_benchmark.ino.bin
/tmp/wingie2-esptool-venv/bin/esptool.py --chip esp32 \
  --port /dev/cu.usbserial-11310 --baud 115200 verify_flash \
  0x10000 /tmp/wingie2-dsp-benchmark-baseline/build/wingie2_dsp_benchmark.ino.bin
```

Capture serial through reset until the single `DSP_BENCH` line appears. Repeat with:

```text
/tmp/wingie2-dsp-benchmark-tf2np/build/wingie2_dsp_benchmark.ino.bin
```

Expected for both: `blocks=100000`, `deadline_misses=0`, `p99_us <= 580.5`, and `max_us < 725.6`. If candidate fails, restore `/tmp/wingie2-before-tf2np-app0.bin`, record the metrics, reverse only the uncommitted `Wingie2.dsp` change with `apply_patch`, and stop this plan before generating product files.

- [ ] **Step 6: Restore product app0 after standalone benchmarking**

Run:

```bash
/tmp/wingie2-esptool-venv/bin/esptool.py --chip esp32 \
  --port /dev/cu.usbserial-11310 --baud 115200 write_flash \
  0x10000 /tmp/wingie2-before-tf2np-app0.bin
/tmp/wingie2-esptool-venv/bin/esptool.py --chip esp32 \
  --port /dev/cu.usbserial-11310 --baud 115200 verify_flash \
  0x10000 /tmp/wingie2-before-tf2np-app0.bin
```

Expected: digest matches and the original firmware starts with both wet paths audible.

### Task 6: Regenerate and Compile Product Firmware

**Files:**
- Modify: `Wingie2.dsp`
- Regenerate: `Wingie2/Wingie2.cpp`
- Regenerate: `Wingie2/Wingie2.h`

- [ ] **Step 1: Generate candidate product files in a temporary directory**

Run:

```bash
rm -rf /tmp/wingie2-tf2np-generated
mkdir -p /tmp/wingie2-tf2np-generated
cp Wingie2.dsp /tmp/wingie2-tf2np-generated/Wingie2.dsp
cd /tmp/wingie2-tf2np-generated
faust2esp32 -ac101 -lib Wingie2.dsp
cd /Users/mengwu/Documents/Code/Wingie2
cp /tmp/wingie2-tf2np-generated/Wingie2/Wingie2.cpp Wingie2/Wingie2.cpp
cp /tmp/wingie2-tf2np-generated/Wingie2/Wingie2.h Wingie2/Wingie2.h
```

- [ ] **Step 2: Review generated changes as one unit**

Run:

```bash
git diff --stat -- Wingie2.dsp Wingie2/Wingie2.cpp Wingie2/Wingie2.h
git diff --check
rg -n "sqrt\(|tf2np|stableModeFilter" Wingie2.dsp Wingie2/Wingie2.cpp
git diff -- Wingie2.dsp
```

Expected: source diff contains only `stableModeFilter` and the `r()` call replacement; generated files contain the corresponding normalized-ladder state/math and no unrelated architecture replacement.

- [ ] **Step 3: Build normal and diagnostic firmware**

Run:

```bash
arduino-cli board details --fqbn esp32:esp32:esp32 | \
  rg 'Board version:.*2\.0\.4'
rm -rf /tmp/wingie2-tf2np-normal-build /tmp/wingie2-tf2np-diag-build
arduino-cli compile --fqbn esp32:esp32:esp32 --libraries Libraries \
  --output-dir /tmp/wingie2-tf2np-normal-build Wingie2
arduino-cli compile --fqbn esp32:esp32:esp32 --libraries Libraries \
  --build-property compiler.cpp.extra_flags=-DMIDI_DIAGNOSTICS=1 \
  --output-dir /tmp/wingie2-tf2np-diag-build Wingie2
```

Expected: board details resolves to Core 2.0.4 (the local mirror package may display `2.0.4-cn`); both builds compile, both images fit app0, and RAM remains below 327,680 bytes.

- [ ] **Step 4: Commit the candidate DSP only after static/CPU gates pass**

```bash
git add Wingie2.dsp Wingie2/Wingie2.cpp Wingie2/Wingie2.h
git commit -m "fix: 使用 TF2NP 稳定 mode filters"
```

### Task 7: Run Full Hardware Stability and A/B

**Files:**
- Read: `/dev/cu.usbserial-11310`
- Write hardware: app0 at `0x10000` only
- Create: `docs/superpowers/results/2026-07-10-mode-filter-tf2np-results.md`

- [ ] **Step 1: Validate and flash diagnostic candidate app0**

Run:

```bash
/tmp/wingie2-esptool-venv/bin/esptool.py --chip esp32 \
  image_info /tmp/wingie2-tf2np-diag-build/Wingie2.ino.bin
shasum -a 256 /tmp/wingie2-tf2np-diag-build/Wingie2.ino.bin
/tmp/wingie2-esptool-venv/bin/esptool.py --chip esp32 \
  --port /dev/cu.usbserial-11310 --baud 115200 write_flash \
  0x10000 /tmp/wingie2-tf2np-diag-build/Wingie2.ino.bin
/tmp/wingie2-esptool-venv/bin/esptool.py --chip esp32 \
  --port /dev/cu.usbserial-11310 --baud 115200 verify_flash \
  0x10000 /tmp/wingie2-tf2np-diag-build/Wingie2.ino.bin
```

Expected: image validation and app0 verify succeed; no other flash offset is written.

- [ ] **Step 2: Start one persistent serial session**

Run `/tmp/wingie2-midi-tools-venv/bin/python Tools/midi_diag_session.py` in a persistent terminal session. Keep this exact serial connection open through every `r` reset, configuration check, stress command, marker, and `p` snapshot below.

Expected: no `POWERON_RESET` between reset, stress, and snapshot.

- [ ] **Step 3: Run String mode long stress on both channels**

Configure channel 1:

```bash
swift Tools/midi_stress.swift config 1 1 16383
```

Send `p` in the serial session and confirm channel 1 reports mode 1. Then send `r`, wait for `MIDI_DIAG reset`, and run the batch:

```bash
swift Tools/midi_stress.swift batch 1 100000 1000
```

Send `p` immediately after the batch, before either marker. Confirm `parsed=200000`, channel 1 `on=100000 off=100000`, and errors 0. Then run:

```bash
swift Tools/midi_stress.swift marker 1 60
swift Tools/midi_stress.swift marker 2 67
```

Send a final `p` and confirm both marker pitches reached their corresponding DSP parameters.

Configure channel 2:

```bash
swift Tools/midi_stress.swift config 2 1 16383
```

Send `p` and confirm channel 2 reports mode 1. Then send `r`, wait for `MIDI_DIAG reset`, and run:

```bash
swift Tools/midi_stress.swift batch 2 100000 1000
```

Send `p` immediately after the batch. Confirm `parsed=200000`, channel 2 `on=100000 off=100000`, and errors 0. Then run:

```bash
swift Tools/midi_stress.swift marker 2 60
swift Tools/midi_stress.swift marker 1 67
```

Send a final `p` and confirm both marker pitches reached their corresponding DSP parameters.

Expected per channel before markers: `parsed=200000`, target `on=100000 off=100000`, errors 0; target wet, control wet, and dry/thru remain audible. After markers, both target and control DSP pitch parameters update.

- [ ] **Step 4: Run Poly and Bar coverage on both channels**

Run each case below independently. Configuration happens before the counter reset so its three CC messages cannot contaminate the batch count.

Poly, channel 1:

```bash
swift Tools/midi_stress.swift config 1 0 16383
```

Send `p` and confirm channel 1 mode 0, then send `r` and wait for `MIDI_DIAG reset`. Run:

```bash
swift Tools/midi_stress.swift batch 1 10000 1000
```

Send `p` and confirm `parsed=20000`, channel 1 `on=10000 off=10000`, and errors 0. Then run:

```bash
swift Tools/midi_stress.swift marker 1 60
swift Tools/midi_stress.swift marker 2 67
```

Send a final `p` and confirm both marker parameters update.

Poly, channel 2:

```bash
swift Tools/midi_stress.swift config 2 0 16383
```

Send `p` and confirm channel 2 mode 0, then send `r` and wait for `MIDI_DIAG reset`. Run:

```bash
swift Tools/midi_stress.swift batch 2 10000 1000
```

Send `p` and confirm `parsed=20000`, channel 2 `on=10000 off=10000`, and errors 0. Then run:

```bash
swift Tools/midi_stress.swift marker 2 60
swift Tools/midi_stress.swift marker 1 67
```

Send a final `p` and confirm both marker parameters update.

Bar, channel 1:

```bash
swift Tools/midi_stress.swift config 1 2 16383
```

Send `p` and confirm channel 1 mode 2, then send `r` and wait for `MIDI_DIAG reset`. Run:

```bash
swift Tools/midi_stress.swift batch 1 10000 1000
```

Send `p` and confirm `parsed=20000`, channel 1 `on=10000 off=10000`, and errors 0. Then run:

```bash
swift Tools/midi_stress.swift marker 1 60
swift Tools/midi_stress.swift marker 2 67
```

Send a final `p` and confirm both marker parameters update.

Bar, channel 2:

```bash
swift Tools/midi_stress.swift config 2 2 16383
```

Send `p` and confirm channel 2 mode 2, then send `r` and wait for `MIDI_DIAG reset`. Run:

```bash
swift Tools/midi_stress.swift batch 2 10000 1000
```

Send `p` and confirm `parsed=20000`, channel 2 `on=10000 off=10000`, and errors 0. Then run:

```bash
swift Tools/midi_stress.swift marker 2 60
swift Tools/midi_stress.swift marker 1 67
```

Send a final `p` and confirm both marker parameters update.

Expected per run: `parsed=20000`, target `on=10000 off=10000`, errors 0, all marker parameters update, both wet paths and dry/thru remain audible, and no audio dropout/reset occurs. If any batch fails, stop the remaining matrix, keep the serial session open, capture one final `p` snapshot, and preserve the failed audio state until it is documented; do not hide it with another candidate change.

- [ ] **Step 5: Perform baseline/candidate listening A/B**

Switch only app0 between `/tmp/wingie2-before-tf2np-app0.bin` and `/tmp/wingie2-tf2np-normal-build/Wingie2.ino.bin`, verifying each write. With identical source, panel positions, tuning, modes, and notes, have the user compare:

1. Static pitches in Poly, String, and Bar.
2. Decay at minimum, midpoint, and maximum.
3. Input dynamics and wet level.
4. Slow note changes and rapid MIDI changes.

Expected: user explicitly accepts candidate pitch, T60, gain, and interaction. Numerical gates do not substitute for this decision.

- [ ] **Step 6: Confirm final device image**

Ask the user whether the device should finish on verified baseline or verified candidate app0. Write and verify exactly that image at `0x10000`, then confirm startup MIDI 1/2/3, A3 440, Poly modes, standard tuning, and both wet paths.

- [ ] **Step 7: Record and commit complete results**

Create `docs/superpowers/results/2026-07-10-mode-filter-tf2np-results.md` containing:

- Faust compiler/`filters.lib` hashes and parity result;
- every static metric case summary and `/tmp/mode_filter_metrics.json` aggregate;
- baseline/candidate benchmark median, p95, p99, max, deadline misses, program size, and RAM;
- one row per hardware batch with sent/parsed/callback/errors/RX high-water/marker/audio result;
- separate user A/B conclusion;
- baseline/candidate app0 hashes and final device image choice.

Do not claim PASS for a physical or listening result without explicit user confirmation.

Then run:

```bash
git add docs/superpowers/results/2026-07-10-mode-filter-tf2np-results.md
git commit -m "test: 记录 TF2NP 真机验证结果"
git status --short
```

Expected: result commit succeeds and the worktree is clean. If any acceptance gate failed, the results must say the candidate was rejected; do not merge the candidate DSP commit into the product branch.
