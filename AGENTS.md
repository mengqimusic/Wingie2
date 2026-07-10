# Repository Guidelines

## Project Structure & Module Organization

`Wingie2/` is the Arduino sketch: `Wingie2.ino` is the entry point, the other `.ino` tabs separate control, MIDI, I2C, tuning, and utility logic, and `AC101.*` drives the codec. The root `Wingie2.dsp` is the Faust DSP source; `Wingie2/Wingie2.cpp` contains generated Faust output. Keep vendored I2C support in `Libraries/I2Cdev/`. `Tools/` contains Pure Data and TouchOSC utilities, while `doc/` and `ALT_TUNING.md` hold tuning references. Treat tracked ZIP files as release artifacts, not routine edit targets.

## Build, Test, and Development Commands

Use the `ESP32 Dev Module` target and ESP32 Arduino Core `2.0.4`. The README flags 2.0.5 as incompatible, and current 3.x cores no longer provide legacy ESP-IDF APIs used here. Install the external libraries named in the README, then compile:

```bash
arduino-cli core install esp32:esp32@2.0.4 --additional-urls https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
arduino-cli lib install "MIDI Library" "Adafruit AW9523"
arduino-cli compile --fqbn esp32:esp32:esp32 --libraries Libraries Wingie2
```

After changing `Wingie2.dsp`, run `faust2esp32 -ac101 -lib Wingie2.dsp`, replace the generated sketch outputs together, and review the generated diff.

## Coding Style & Naming Conventions

There is no enforced formatter. Match the surrounding file: Arduino tabs generally use two-space indentation, while codec C++ uses four spaces. Preserve established hardware terminology, use `UPPER_SNAKE_CASE` for macros, and retain existing lower-camel names for hardware arrays. Do not hand-format generated Faust C++; make DSP changes in `Wingie2.dsp` and regenerate it.

## Testing Guidelines

No automated test suite or coverage threshold is configured. Every firmware change must pass the full Arduino CLI compile above. Hardware-facing changes also require a Wingie2 smoke test covering the affected audio channel, MIDI messages, I2C controls, tuning mode, or saved Preferences. Report compilation and physical-device validation separately; a successful build does not prove hardware behavior.

## Commit & Pull Request Guidelines

History favors short, imperative summaries such as `Fix typos in tuning readme` and `Retune caves to default`. Keep each commit focused on one concern. Pull requests should explain the behavior and motivation, identify Core or library requirements, link relevant issues, and report build plus hardware results. Include screenshots for Pure Data or TouchOSC changes and attach useful MIDI traces or serial logs when behavior changes.
