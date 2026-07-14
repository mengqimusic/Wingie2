#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "faust/dsp/dsp.h"
#include "faust/gui/MapUI.h"
#include "faust/gui/meta.h"

<<includeIntrinsic>>
<<includeclass>>

namespace {

constexpr int kSampleRate = 44100;
constexpr int kBlockSize = 32;
constexpr float kPi = 3.14159265358979323846f;

void setRequired(MapUI& ui, const std::string& path, float value) {
  FAUSTFLOAT* zone = ui.getParamZone(path);
  if (!zone) {
    std::cerr << "missing parameter: " << path << "\n";
    std::exit(4);
  }
  *zone = value;
}

bool setOptional(MapUI& ui, const std::string& path, float value) {
  FAUSTFLOAT* zone = ui.getParamZone(path);
  if (!zone) return false;
  *zone = value;
  return true;
}

void setModeChanged(MapUI& ui, float value) {
  setRequired(ui, "/Wingie/left/mode_changed", value);
  setRequired(ui, "/Wingie/right/mode_changed", value);
}

void setMode(MapUI& ui, int mode) {
  setRequired(ui, "mode0", float(mode));
  setRequired(ui, "mode1", float(mode));
}

void configure(MapUI& ui, bool enabled, float energyLimit, float rhoGuard) {
  setRequired(ui, "a3_freq", 440.0f);
  setRequired(ui, "use_alt_tuning", 0.0f);
  setRequired(ui, "resonator_input_gain", 0.25f);
  setRequired(ui, "pre_clip_gain", 0.2475f);
  setRequired(ui, "post_clip_gain", 0.825f);
  setRequired(ui, "volume0", 1.0f);
  setRequired(ui, "volume1", 1.0f);
  setRequired(ui, "mix0", 1.0f);
  setRequired(ui, "mix1", 1.0f);
  setRequired(ui, "/Wingie/left/decay", 10.0f);
  setRequired(ui, "/Wingie/right/decay", 10.0f);
  setMode(ui, 3);
  setModeChanged(ui, 0.0f);
  constexpr float frequencies[9] = {
      440.0f, 673.0f, 997.0f, 1481.0f, 2203.0f,
      3271.0f, 4861.0f, 7219.0f, 11003.0f,
  };
  for (int channel = 0; channel < 2; ++channel) {
    const std::string side = channel == 0 ? "left" : "right";
    for (int index = 0; index < 9; ++index) {
      setRequired(ui, "/Wingie/" + side + "/cave_freq_" +
                          std::to_string(index),
                  frequencies[index]);
      setRequired(ui, "/Wingie/" + side + "/mute_" +
                          std::to_string(index),
                  index == 0 ? 0.0f : 1.0f);
    }
  }
  const bool hasController = setOptional(ui, "anti_feedback_enabled",
                                         enabled ? 1.0f : 0.0f);
  if (hasController) {
    setRequired(ui, "anti_feedback_energy_limit", energyLimit);
    setRequired(ui, "anti_feedback_rho_guard", rhoGuard);
  } else if (enabled) {
    std::cerr << "baseline DSP cannot enable anti-feedback\n";
    std::exit(5);
  }
}

float noise(uint32_t& state) {
  state = state * 1664525u + 1013904223u;
  return float(int32_t(state)) / 2147483648.0f;
}

float sourceFor(const std::string& scenario, int channel, int sampleIndex,
                uint32_t& noiseState) {
  const float phase = 2.0f * kPi * 440.0f * float(sampleIndex) / kSampleRate;
  if (scenario == "normal") {
    const float impulse = sampleIndex % 22050 == 0 ? 0.5f : 0.0f;
    return impulse + 0.02f * noise(noiseState);
  }
  if (scenario == "transient") {
    return sampleIndex == 0 ? 1.0f : 0.0f;
  }
  if (scenario == "sustained") {
    return 0.25f * std::sin(phase + channel * 0.5f);
  }
  if (scenario == "rapid-notes" || scenario == "mode-changes") {
    return 0.08f * noise(noiseState);
  }
  if (scenario == "feedback" || scenario == "isolation") {
    if (sampleIndex < kSampleRate / 4 && (scenario != "isolation" || channel == 0)) {
      return 0.02f * std::sin(phase);
    }
    return scenario == "isolation" && channel == 1
               ? 0.005f * noise(noiseState)
               : 0.0f;
  }
  return 0.0f;
}

int renderSamplesFor(const std::string& scenario) {
  return (scenario == "feedback" || scenario == "isolation")
             ? 20 * kSampleRate
             : 10 * kSampleRate;
}

bool writeFloat(std::ofstream& stream, float value) {
  stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
  return stream.good();
}

}

int main(int argc, char** argv) {
  if (argc != 9) {
    std::cerr << "usage: render_generated_wingie_anti_feedback "
                 "<normal|transient|sustained|rapid-notes|mode-changes|"
                 "feedback|isolation> <enabled-0-1> <energy-limit> "
                 "<rho-guard> <source-gain> <feedback-gain> "
                 "<feedback-delay-samples> <output.f32>\n";
    return 2;
  }
  const std::string scenario = argv[1];
  const bool validScenario =
      scenario == "normal" || scenario == "transient" ||
      scenario == "sustained" || scenario == "rapid-notes" ||
      scenario == "mode-changes" || scenario == "feedback" ||
      scenario == "isolation";
  if (!validScenario) return 3;
  const bool enabled = std::atoi(argv[2]) != 0;
  const float energyLimit = std::strtof(argv[3], nullptr);
  const float rhoGuard = std::strtof(argv[4], nullptr);
  const float sourceGain = std::strtof(argv[5], nullptr);
  const float feedbackGain = std::strtof(argv[6], nullptr);
  const int feedbackDelay = std::atoi(argv[7]);
  if (feedbackDelay < kBlockSize) return 4;

  mydsp processor;
  processor.init(kSampleRate);
  MapUI ui;
  processor.buildUserInterface(&ui);
  configure(ui, enabled, energyLimit, rhoGuard);

  float inputLeft[kBlockSize] = {};
  float inputRight[kBlockSize] = {};
  float outputLeft[kBlockSize] = {};
  float outputRight[kBlockSize] = {};
  float* inputs[] = {inputLeft, inputRight};
  float* outputs[] = {outputLeft, outputRight};
  for (int frame = 0; frame < kSampleRate; frame += kBlockSize) {
    processor.compute(kBlockSize, inputs, outputs);
  }

  std::ofstream stream(argv[8], std::ios::binary);
  if (!stream) return 6;
  std::vector<std::vector<float>> feedback(
      2, std::vector<float>(feedbackDelay, 0.0f));
  int feedbackPosition = 0;
  uint32_t noiseState[2] = {0x51a7f00du, 0x9e3779b9u};
  const int renderSamples = renderSamplesFor(scenario);
  const int blockCount = (renderSamples + kBlockSize - 1) / kBlockSize;

  for (int block = 0; block < blockCount; ++block) {
    if (scenario == "rapid-notes") {
      setMode(ui, 1);
      const float note = 36.0f + float((block * 7) % 60);
      setRequired(ui, "note0", note);
      setRequired(ui, "note1", note);
    } else if (scenario == "mode-changes" && block % 96 == 0) {
      setMode(ui, (block / 96) % 4);
      setModeChanged(ui, 1.0f);
    } else if (scenario == "mode-changes" && block % 96 == 8) {
      setModeChanged(ui, 0.0f);
    }

    for (int frame = 0; frame < kBlockSize; ++frame) {
      const int sampleIndex = block * kBlockSize + frame;
      const int delayIndex = (feedbackPosition + frame) % feedbackDelay;
      const bool feedbackActive = sampleIndex < 10 * kSampleRate;
      const float leftLoop =
          feedbackActive ? feedbackGain * feedback[0][delayIndex] : 0.0f;
      const float rightLoop =
          feedbackActive && scenario != "isolation"
              ? feedbackGain * feedback[1][delayIndex]
              : 0.0f;
      inputLeft[frame] = sourceGain *
                             sourceFor(scenario, 0, sampleIndex, noiseState[0]) +
                         leftLoop;
      inputRight[frame] = sourceGain *
                              sourceFor(scenario, 1, sampleIndex, noiseState[1]) +
                          rightLoop;
    }
    processor.compute(kBlockSize, inputs, outputs);
    for (int frame = 0; frame < kBlockSize; ++frame) {
      const int delayIndex = (feedbackPosition + frame) % feedbackDelay;
      feedback[0][delayIndex] = outputLeft[frame];
      feedback[1][delayIndex] = outputRight[frame];
      if (!std::isfinite(outputLeft[frame]) ||
          !std::isfinite(outputRight[frame]) ||
          !writeFloat(stream, outputLeft[frame]) ||
          !writeFloat(stream, outputRight[frame])) {
        return 7;
      }
    }
    feedbackPosition = (feedbackPosition + kBlockSize) % feedbackDelay;
  }
  return stream.good() ? 0 : 8;
}
