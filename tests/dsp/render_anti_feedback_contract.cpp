#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "faust/dsp/dsp.h"
#include "faust/gui/MapUI.h"
#include "faust/gui/meta.h"

<<includeIntrinsic>>
<<includeclass>>

namespace {

constexpr int kSampleRate = 44100;
constexpr int kBlockSize = 32;
constexpr int kBlockCount = 16;

void setRequired(MapUI& ui, const std::string& path, float value) {
  FAUSTFLOAT* zone = ui.getParamZone(path);
  if (!zone) {
    std::cerr << "missing parameter: " << path << "\n";
    std::exit(4);
  }
  *zone = value;
}

float excitationFor(const std::string& scenario, int sampleIndex,
                    uint32_t& noiseState) {
  if (scenario == "inactive") {
    noiseState = noiseState * 1664525u + 1013904223u;
    return float(int32_t(noiseState)) / 2147483648.0f * 0.001f;
  }
  if (scenario == "overload-recovery") {
    return sampleIndex == 0 ? 1.0f : 0.0f;
  }
  if (scenario == "sustained") {
    return sampleIndex < 8 * kBlockSize ? 0.25f : 0.0f;
  }
  if (scenario == "bypass") {
    return sampleIndex % 29 == 0 ? 1.5f : -0.05f;
  }
  return 0.0f;
}

float frequencyFor(int block) {
  constexpr float frequencies[] = {440.0f, 440.0f, 16000.0f, 16.0f,
                                   7040.0f, 1379.0f, 1379.0f, 1379.0f};
  return frequencies[block < 8 ? block : 7];
}

bool writeFloat(std::ofstream& stream, float value) {
  stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
  return stream.good();
}

}

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: render_anti_feedback_contract "
                 "<inactive|overload-recovery|sustained|bypass> <output.f32>\n";
    return 2;
  }
  const std::string scenario = argv[1];
  if (scenario != "inactive" && scenario != "overload-recovery" &&
      scenario != "sustained" && scenario != "bypass") {
    return 3;
  }

  mydsp processor;
  processor.init(kSampleRate);
  MapUI ui;
  processor.buildUserInterface(&ui);
  setRequired(ui, "t60", 10.0f);
  setRequired(ui, "energy_limit", scenario == "inactive" ? 64.0f : 0.25f);
  setRequired(ui, "rho_guard", 0.5f);
  setRequired(ui, "enabled", scenario == "bypass" ? 0.0f : 1.0f);

  std::ofstream stream(argv[2], std::ios::binary);
  if (!stream) return 5;

  float input[kBlockSize] = {};
  float q[kBlockSize] = {};
  float p[kBlockSize] = {};
  float peak[kBlockSize] = {};
  float gain[kBlockSize] = {};
  float effectiveRho[kBlockSize] = {};
  float* inputs[] = {input};
  float* outputs[] = {q, p, peak, gain, effectiveRho};
  uint32_t noiseState = 0x51a7f00du;

  for (int block = 0; block < kBlockCount; ++block) {
    const float frequency = frequencyFor(block);
    setRequired(ui, "frequency", frequency);
    for (int frame = 0; frame < kBlockSize; ++frame) {
      const int sampleIndex = block * kBlockSize + frame;
      input[frame] = excitationFor(scenario, sampleIndex, noiseState);
    }
    processor.compute(kBlockSize, inputs, outputs);
    for (int frame = 0; frame < kBlockSize; ++frame) {
      const float values[] = {input[frame], frequency, q[frame], p[frame],
                              peak[frame], gain[frame], effectiveRho[frame]};
      for (float value : values) {
        if (!std::isfinite(value) || !writeFloat(stream, value)) return 6;
      }
      if (gain[frame] < 0.0f || gain[frame] > 1.0f ||
          effectiveRho[frame] <= 0.0f || effectiveRho[frame] >= 1.0f) {
        return 7;
      }
    }
  }
  return stream.good() ? 0 : 8;
}
