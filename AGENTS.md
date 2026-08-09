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

Flashing always uses `UploadSpeed=460800`; the default 921600 fails intermittently at sync over the desk USB hub / CH340 link:

```bash
arduino-cli upload --fqbn esp32:esp32:esp32:UploadSpeed=460800 -p /dev/cu.usbserial-11410 Wingie2
```

If you bypass arduino-cli and drive esptool directly (e.g. writing a release package's
four images), always pass `--flash_mode dio`. The released bootloader carries a QIO header;
writing it as-is puts the device in a ROM boot loop (`load:0xa0c263a0` garbage on GD25Q32
flash), while the web flasher (manifest `"mode": "dio"`) and arduino-cli rewrite the header
to DIO automatically. Verified on hardware during the v4.10 gate run.

After changing `Wingie2.dsp`, run `faust2esp32 -ac101 -lib Wingie2.dsp`, replace the generated sketch outputs together, and review the generated diff.

## Coding Style & Naming Conventions

There is no enforced formatter. Match the surrounding file: Arduino tabs generally use two-space indentation, while codec C++ uses four spaces. Preserve established hardware terminology, use `UPPER_SNAKE_CASE` for macros, and retain existing lower-camel names for hardware arrays. Do not hand-format generated Faust C++; make DSP changes in `Wingie2.dsp` and regenerate it.

## Testing Guidelines

The repository has two automated test layers. Run them before considering a change verified:

- **Host unit tests** (`tests/host/*.cpp`, 4 files) cover the header-only modules `tap_sequence.h`, `mpe_state.h`, `config_profiles.h`, and `serial_config_protocol.h`. These headers are intentionally Arduino-free (only standard C headers), so compile each test directly:
  ```bash
  g++ -std=c++17 -I. tests/host/<name>_test.cpp -o /tmp/<name>_test && /tmp/<name>_test
  ```
- **Python tests** (`tests/dsp/`, `tests/tools/`, 89 cases total) cover the web config/flasher HTML pages, the firmware release toolchain, the mode-filter flash tool, DSP reference models, and Faust source extraction:
  ```bash
  python3 -m pytest tests/ -q
  ```

There is no CI runner, no `conftest.py`, and no coverage threshold; tests run locally only. The Faust-generated DSP core (`Wingie2/Wingie2.cpp`) and the main firmware control flow (`Wingie2.ino`, `control.ino`, `MIDI.ino`, `MPE.ino`, `serial_config.ino`) have no host-level tests and rely on compile + hardware validation.

Every firmware change must additionally pass the full Arduino CLI compile above. Hardware-facing changes also require a Wingie2 smoke test covering the affected audio channel, MIDI messages, I2C controls, tuning mode, or saved Preferences. Report compilation and physical-device validation separately; a successful build does not prove hardware behavior.

Do not read back or back up the device's current app0 image before routine product or candidate flashing. If a rollback is needed, check out the known Git commit and rebuild it with the pinned toolchain; Git source and reproducible builds are the recovery source of truth.

## Commit & Pull Request Guidelines

History favors short, imperative summaries such as `Fix typos in tuning readme` and `Retune caves to default`. Keep each commit focused on one concern. Pull requests should explain the behavior and motivation, identify Core or library requirements, link relevant issues, and report build plus hardware results. Include screenshots for Pure Data or TouchOSC changes and attach useful MIDI traces or serial logs when behavior changes.
