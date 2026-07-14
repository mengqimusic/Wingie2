#include <Arduino.h>
#include <cmath>
#include <cstdint>
#include <xtensa/hal.h>

#include "faust/dsp/dsp.h"
#include "faust/gui/MapUI.h"
#include "faust/gui/meta.h"
#include "generated_user.h"

namespace {

constexpr int kSampleRate = 44100;
constexpr int kBlockSize = 32;
constexpr uint32_t kWarmupBlocks = 10000;
constexpr uint32_t kMeasuredBlocks = 100000;
constexpr uint16_t kHistogramBins = 8192;
constexpr float kPi = 3.14159265358979323846f;

struct BenchmarkCase {
  const char* name;
  int activeModes;
};

constexpr BenchmarkCase kCases[] = {
    {"inactive", 0},
    {"single_active", 1},
    {"all_active", 18},
};

mydsp processor;
MapUI ui;
float inputLeft[kBlockSize];
float inputRight[kBlockSize];
float outputLeft[kBlockSize];
float outputRight[kBlockSize];
float* inputs[] = {inputLeft, inputRight};
float* outputs[] = {outputLeft, outputRight};
uint32_t histogram[kHistogramBins];
uint32_t randomState = 0x57494e47u;
uint32_t maxCycles = 0;
uint32_t deadlineMisses = 0;
uint32_t nonfiniteOutputs = 0;
uint32_t cpuMHz = 0;
volatile float outputChecksum = 0.0f;
float phase = 0.0f;

float noise() {
  randomState = randomState * 1664525u + 1013904223u;
  return float(int32_t(randomState)) / 2147483648.0f;
}

void setRequired(const char* path, float value) {
  FAUSTFLOAT* zone = ui.getParamZone(path);
  if (!zone) {
    Serial.printf("DSP_BENCH missing_parameter=%s\n", path);
    while (true) delay(1000);
  }
  *zone = value;
}

void setCaveFrequency(int channel, int index, float frequency) {
  char path[48];
  snprintf(path, sizeof(path), "/Wingie/%s/cave_freq_%d",
           channel == 0 ? "left" : "right", index);
  setRequired(path, frequency);
}

void configureCommon() {
  setRequired("mode0", 3.0f);
  setRequired("mode1", 3.0f);
  setRequired("/Wingie/left/decay", 10.0f);
  setRequired("/Wingie/right/decay", 10.0f);
  setRequired("volume0", 1.0f);
  setRequired("volume1", 1.0f);
  setRequired("mix0", 1.0f);
  setRequired("mix1", 1.0f);
  setRequired("resonator_input_gain", 1.0f);
  setRequired("pre_clip_gain", 1.0f);
  setRequired("post_clip_gain", 1.0f);
  setRequired("anti_feedback_enabled", 1.0f);
  setRequired("anti_feedback_energy_limit", 1.0f);
  setRequired("anti_feedback_rho_guard", 0.998435f);
}

void configureCase(const BenchmarkCase& benchmarkCase) {
  constexpr float distinctFrequencies[9] = {
      440.0f, 673.0f, 997.0f, 1481.0f, 2203.0f,
      3271.0f, 4861.0f, 7219.0f, 11003.0f,
  };
  for (int channel = 0; channel < 2; ++channel) {
    for (int index = 0; index < 9; ++index) {
      const bool allActive = benchmarkCase.activeModes == 18;
      setCaveFrequency(channel, index,
                       allActive ? 440.0f : distinctFrequencies[index]);
    }
  }
}

void fillInput(const BenchmarkCase& benchmarkCase) {
  const float phaseStep = 2.0f * kPi * 440.0f / kSampleRate;
  for (int index = 0; index < kBlockSize; ++index) {
    if (benchmarkCase.activeModes == 0) {
      inputLeft[index] = 0.001f * noise();
      inputRight[index] = 0.001f * noise();
    } else {
      const float sample = 0.25f * std::sin(phase);
      inputLeft[index] = sample;
      inputRight[index] = benchmarkCase.activeModes == 18 ? sample : 0.0f;
      phase += phaseStep;
      if (phase >= 2.0f * kPi) phase -= 2.0f * kPi;
    }
  }
}

void resetMetrics() {
  memset(histogram, 0, sizeof(histogram));
  maxCycles = 0;
  deadlineMisses = 0;
  nonfiniteOutputs = 0;
  outputChecksum = 0.0f;
  phase = 0.0f;
}

uint32_t percentile(uint32_t numerator, uint32_t denominator) {
  const uint32_t target =
      (kMeasuredBlocks * numerator + denominator - 1) / denominator;
  uint32_t accumulated = 0;
  for (uint32_t bin = 0; bin < kHistogramBins; ++bin) {
    accumulated += histogram[bin];
    if (accumulated >= target) return bin;
  }
  return kHistogramBins - 1;
}

void inspectOutput() {
  for (int index = 0; index < kBlockSize; ++index) {
    if (!std::isfinite(outputLeft[index]) ||
        !std::isfinite(outputRight[index])) {
      nonfiniteOutputs++;
    }
    outputChecksum += outputLeft[index] + outputRight[index];
  }
}

void runCase(const BenchmarkCase& benchmarkCase) {
  processor.instanceClear();
  configureCase(benchmarkCase);
  resetMetrics();
  for (uint32_t block = 0; block < kWarmupBlocks; ++block) {
    fillInput(benchmarkCase);
    processor.compute(kBlockSize, inputs, outputs);
    inspectOutput();
  }

  const uint64_t deadlineCycles =
      uint64_t(cpuMHz) * 1000000ULL * kBlockSize / kSampleRate;
  for (uint32_t block = 0; block < kMeasuredBlocks; ++block) {
    fillInput(benchmarkCase);
    const uint32_t started = xthal_get_ccount();
    processor.compute(kBlockSize, inputs, outputs);
    const uint32_t cycles = xthal_get_ccount() - started;
    if (cycles > maxCycles) maxCycles = cycles;
    if (cycles >= deadlineCycles) deadlineMisses++;
    const uint32_t tenthsOfMicrosecond =
        uint32_t((uint64_t(cycles) * 10 + cpuMHz - 1) / cpuMHz);
    const uint32_t bin = tenthsOfMicrosecond < kHistogramBins
                             ? tenthsOfMicrosecond
                             : kHistogramBins - 1;
    histogram[bin]++;
    inspectOutput();
    if ((block & 255u) == 0) delay(1);
  }
  Serial.printf(
      "DSP_BENCH case=%s blocks=%lu cpu_mhz=%lu median_us=%.1f "
      "p95_us=%.1f p99_us=%.1f max_us=%.3f deadline_misses=%lu "
      "nonfinite_outputs=%lu checksum=%.9g\n",
      benchmarkCase.name,
      (unsigned long)kMeasuredBlocks,
      (unsigned long)cpuMHz,
      double(percentile(50, 100)) / 10.0,
      double(percentile(95, 100)) / 10.0,
      double(percentile(99, 100)) / 10.0,
      double(maxCycles) / cpuMHz,
      (unsigned long)deadlineMisses,
      (unsigned long)nonfiniteOutputs,
      double(outputChecksum));
}

}

void setup() {
  Serial.begin(115200);
  delay(1000);
  processor.init(kSampleRate);
  processor.buildUserInterface(&ui);
  configureCommon();
  cpuMHz = getCpuFrequencyMhz();
  for (const BenchmarkCase& benchmarkCase : kCases) {
    runCase(benchmarkCase);
  }
}

void loop() {
  delay(1000);
}
