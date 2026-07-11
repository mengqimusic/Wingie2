# Block-Rate Modal Bank Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the rapidly retuned direct-form modal filters with a low-cost block-coefficient rotation bank that remains bounded under arbitrary note and future-ratio jumps while preserving one accurate narrow peak per isolated mode.

**Architecture:** Keep frequency mapping, input conditioning, envelopes, wet/dry, and output processing outside a new nine-mode bank. Each mode retains a two-state `rho R(theta)` recursion at sample rate, while `rho`, `sin(theta)`, `cos(theta)`, and output normalization are prepared only once per 32-sample block. Execute only this independent-mode candidate; a custom normalized-lattice fallback requires a separate design if this candidate fails only the ESP32 deadline.

**Tech Stack:** Faust 2.59.6, C++11, Python 3 with installed NumPy/SciPy, Arduino CLI, ESP32 Arduino Core 2.0.4, Xtensa cycle counter, CoreMIDI, pyserial, esptool 3.3.

---

## Hard Boundaries

- Execute in a new worktree and branch named `fix/block-rate-modal-bank`.
- Do not resume or modify either rejected implementation plan:
  - `docs/superpowers/plans/2026-07-10-mode-filter-tf2np-stability.md`
  - `archive/coupled-mode-filter:docs/superpowers/plans/2026-07-11-coupled-mode-filter.md`
- Fix Faust at `2.59.6` and ESP32 Arduino Core at `2.0.4` (`2.0.4-cn` is accepted). Do not upgrade NumPy, SciPy, esptool, or benchmark dependencies.
- Do not add control-side compensation, smoothing, rate limiting, reset, crossfade, fallback, automatic mute/gain, or a new time constant.
- Frequency and future-ratio jumps retain state and take effect on the next 32-sample block.
- Clip every target frequency to `[16, 16000]` Hz. A clipped 16 kHz mode remains audible; remove the historical automatic upper-cap mute.
- User mute gates only direct output. A muted mode still receives input and advances state.
- Do not implement the future custom-ratio UI, storage, or MIDI mapping. Test its bank contract through the existing nine Cave frequency controls.
- Every hardware stage writes only app0 at `0x10000`, then restores and fully verifies the historical app0.
- A physical audio PASS is valid only after explicit user confirmation.
- Do not use subagents during execution if the user retains the current no-subagent instruction.
- Remove generated `__pycache__` directories before every commit and final status check.

## Fixed Acceptance Constants

Do not adjust these values after seeing candidate results:

```text
sample rate                         44100 Hz
audio block                         32 samples
frequency clip                      [16, 16000] Hz
single-mode pole error              <= 5 cents
significant secondary peak          >= -18 dB relative to the primary
peak finder prominence              >= 6 dB
peak identity render decay          10 seconds
host maximum absolute output        <= 8.0
host finite/state growth tolerance  1e-5
ESP32 measured blocks               100000
ESP32 CPU                           240 MHz
ESP32 p99                           <= 580.5 us
ESP32 max                           < 725.6 us
ESP32 deadline misses               0
```

For peak identity, render `524288` impulse-response samples at Decay `10 s`. Compute a
one-sided `rfft`, convert magnitude to dB relative to the full-band maximum, and call
`scipy.signal.find_peaks(db, prominence=6.0)`. The primary is the maximum local peak.
Reject if any other local peak is at or above `-18 dB`. Pole-angle estimation, not FFT-bin
position, enforces the `+/-5 cents` center-frequency gate.

## File Structure

Selectively import from `archive/coupled-mode-filter`:

- `Tools/midi_diag_session.py` - one persistent serial session.
- `Tools/midi_stress.swift` - note/config/marker sender, later extended for Cave batches.
- `tests/dsp/extract_faust_user.py` and test - user-section extraction.
- `tests/esp32/check_deadline.py` and test - deadline parser.
- `tests/esp32/wingie2_generated_benchmark/*` - generated-class benchmark.
- `tests/esp32/wingie2_product_benchmark/*` - end-to-end instrumentation.

Create candidate gates:

- `tests/dsp/analyze_modal_bank.py` and test - contraction, pole, and single-peak checks.
- `tests/dsp/block_rate_modal_bank_contract.dsp` - isolated rotation contract.
- `tests/dsp/render_block_rate_modal_bank.cpp` - isolated renderer.
- `tests/dsp/check_modal_bank_generated.py` and test - generated-loop checker.
- `tests/dsp/render_generated_wingie_modal.cpp` - full-product renderer.
- `tests/dsp/analyze_generated_wingie_modal.py` and test - full-product gate.
- `docs/superpowers/results/2026-07-11-block-rate-modal-bank-results.md` - evidence.

Modify product files only after explicit physical acceptance:

- `Wingie2.dsp`
- `Wingie2/Wingie2.cpp`
- `Wingie2/Wingie2.h`

## Global Hardware Recovery Protocol

Before any candidate write:

```bash
RECOVERY=/tmp/wingie2-modal-bank-recovery-app0.bin
test "$(shasum -a 256 "$RECOVERY" | awk '{print $1}')" = \
  6da9aca6795cc6ef9352705089c5c8aef58a91dc51d37c73592052ef2fa8c36b
/tmp/wingie2-esptool-venv/bin/esptool.py --chip esp32 image_info "$RECOVERY" \
  | rg 'Checksum:.*valid|Validation Hash:.*valid'
```

Candidate write and verify, with no other address:

```bash
/tmp/wingie2-esptool-venv/bin/esptool.py --chip esp32 \
  --port /dev/cu.usbserial-11310 --baud 115200 write_flash \
  0x10000 "$CANDIDATE_BIN"
/tmp/wingie2-esptool-venv/bin/esptool.py --chip esp32 \
  --port /dev/cu.usbserial-11310 --baud 115200 verify_flash \
  0x10000 "$CANDIDATE_BIN"
```

At the end of every hardware stage, regardless of outcome:

```bash
/tmp/wingie2-esptool-venv/bin/esptool.py --chip esp32 \
  --port /dev/cu.usbserial-11310 --baud 115200 write_flash \
  0x10000 "$RECOVERY"
/tmp/wingie2-esptool-venv/bin/esptool.py --chip esp32 \
  --port /dev/cu.usbserial-11310 --baud 115200 verify_flash \
  0x10000 "$RECOVERY"
rm -f /tmp/wingie2-modal-bank-final-readback.bin
/tmp/wingie2-esptool-venv/bin/esptool.py --chip esp32 \
  --port /dev/cu.usbserial-11310 --no-stub read_flash --no-progress \
  0x10000 0x140000 /tmp/wingie2-modal-bank-final-readback.bin
cmp "$RECOVERY" /tmp/wingie2-modal-bank-final-readback.bin
test "$(shasum -a 256 /tmp/wingie2-modal-bank-final-readback.bin | awk '{print $1}')" = \
  6da9aca6795cc6ef9352705089c5c8aef58a91dc51d37c73592052ef2fa8c36b
```

Capture startup and require MIDI `1/2/3`, A3 `440`, Poly/Poly, standard tuning, followed by
explicit user confirmation that restored left and right wet are normal.

### Task 1: Create the Worktree and Pin Inputs

**Files:**
- Read: `docs/superpowers/specs/2026-07-11-block-rate-modal-bank-design.md`
- Read: `Wingie2.dsp`, `Wingie2/Wingie2.cpp`, `Wingie2/Wingie2.h`
- Create outside repository: `/tmp/wingie2-modal-bank-baseline/`

- [ ] **Step 1: Create the isolated branch/worktree**

```bash
git worktree add -b fix/block-rate-modal-bank \
  .worktrees/block-rate-modal-bank main
cd .worktrees/block-rate-modal-bank
test "$(git branch --show-current)" = fix/block-rate-modal-bank
test -z "$(git status --short)"
```

Expected: named branch, clean linked worktree. Do not implement on `main`.

- [ ] **Step 2: Mount and verify Faust 2.59.6**

```bash
if test ! -f /tmp/Faust-2.59.6-arm64.dmg; then
  curl -L --fail --output /tmp/Faust-2.59.6-arm64.dmg \
    https://github.com/grame-cncm/faust/releases/download/2.59.6/Faust-2.59.6-arm64.dmg
fi
test "$(shasum -a 256 /tmp/Faust-2.59.6-arm64.dmg | awk '{print $1}')" = \
  55a52324615c04fcb23a7e32d5cea48c34647d87dab88e30edbed5ba7a22080c
if ! mount | rg -q '/tmp/faust-2\.59\.6-mount/Faust-2\.59\.6'; then
  rm -rf /tmp/faust-2.59.6-mount
  mkdir -p /tmp/faust-2.59.6-mount
  hdiutil attach -nobrowse -readonly \
    -mountpoint /tmp/faust-2.59.6-mount/Faust-2.59.6 \
    /tmp/Faust-2.59.6-arm64.dmg
fi
export FAUST_ROOT=/tmp/faust-2.59.6-mount/Faust-2.59.6/Faust-2.59.6
export PATH="$FAUST_ROOT/bin:$PATH"
export FAUSTARCH="$FAUST_ROOT/share/faust"
faust --version | rg '2\.59\.6'
shasum -a 256 \
  "$FAUST_ROOT/bin/faust" \
  "$FAUST_ROOT/share/faust/filters.lib" \
  "$FAUST_ROOT/share/faust/oscillators.lib" \
  "$FAUST_ROOT/share/faust/esp32/esp32.cpp" \
  "$FAUST_ROOT/share/faust/esp32/esp32.h" \
  "$FAUST_ROOT/include/faust/audio/esp32-dsp.h"
arduino-cli board details --fqbn esp32:esp32:esp32 | rg 'Board version:.*2\.0\.4'
python3 -c 'import numpy, scipy; print(numpy.__version__, scipy.__version__)'
```

Expected hashes in order:

```text
464d9ba249bea4b3589ad0ba5c7e8bf266b4f7e649523e68a022f73a164032c2
6f82542b67778a5f46edc0fc29bf18b5b7d0337b43689f6f2dd07a08c6da45fa
4f7e5f369400f06c16343240db61bfec56f3f93c96f817250262cbc9c9499d71
a35260d0c5a00ebf81367107bf3f626619a9fd85995b596b06a045ec45c80e45
627c103f5d35aa331ca1e26638cee92f96ab08c511f1603921cd84e260d0f2e3
569c14e6cec03b5d8e51fec6ce5cbd990d685601f580b6a0c16c6429fad25921
```

Expected Python versions are NumPy `2.4.2` and SciPy `1.17.1`. Do not install or upgrade
Python packages.

- [ ] **Step 3: Freeze the historical source baseline**

```bash
rm -rf /tmp/wingie2-modal-bank-baseline
mkdir -p /tmp/wingie2-modal-bank-baseline/Wingie2
cp Wingie2.dsp /tmp/wingie2-modal-bank-baseline/Wingie2.dsp
cp Wingie2/Wingie2.cpp /tmp/wingie2-modal-bank-baseline/Wingie2/Wingie2.cpp
cp Wingie2/Wingie2.h /tmp/wingie2-modal-bank-baseline/Wingie2/Wingie2.h
shasum -a 256 \
  /tmp/wingie2-modal-bank-baseline/Wingie2.dsp \
  /tmp/wingie2-modal-bank-baseline/Wingie2/Wingie2.cpp \
  /tmp/wingie2-modal-bank-baseline/Wingie2/Wingie2.h \
  > /tmp/wingie2-modal-bank-baseline/artifact.sha256
shasum -a 256 --check /tmp/wingie2-modal-bank-baseline/artifact.sha256
chmod -R a-w /tmp/wingie2-modal-bank-baseline
```

Expected: all three entries report `OK`.

- [ ] **Step 4: Compile the baseline and establish recovery identity**

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 --libraries Libraries \
  --output-dir /tmp/wingie2-modal-bank-baseline-build Wingie2
if test ! -f /tmp/wingie2-modal-bank-recovery-app0.bin; then
  /tmp/wingie2-esptool-venv/bin/esptool.py --chip esp32 \
    --port /dev/cu.usbserial-11310 --no-stub read_flash --no-progress \
    0x10000 0x140000 /tmp/wingie2-modal-bank-recovery-app0.bin
fi
test "$(shasum -a 256 /tmp/wingie2-modal-bank-recovery-app0.bin | awk '{print $1}')" = \
  6da9aca6795cc6ef9352705089c5c8aef58a91dc51d37c73592052ef2fa8c36b
```

Expected: baseline compile passes and the read-only recovery image matches history.

- [ ] **Step 5: Import and verify reusable infrastructure**

```bash
git restore --source=archive/coupled-mode-filter -- \
  Tools/midi_diag_session.py Tools/midi_stress.swift \
  tests/dsp/extract_faust_user.py tests/dsp/extract_faust_user_test.py \
  tests/esp32/check_deadline.py tests/esp32/check_deadline_test.py \
  tests/esp32/wingie2_generated_benchmark \
  tests/esp32/wingie2_product_benchmark
python3 tests/dsp/extract_faust_user_test.py
python3 tests/esp32/check_deadline_test.py
FAUST_ROOT="$FAUST_ROOT" \
  python3 tests/esp32/wingie2_product_benchmark/instrument_esp32_architecture_test.py
test -z "$(git diff --name-only -- Wingie2 Wingie2.dsp)"
```

- [ ] **Step 6: Extend the imported MIDI sender before benchmark use**

First verify RED:

```bash
swift Tools/midi_stress.swift --help 2>&1 | rg 'cave-batch'
```

Expected: FAIL. Then allow `config` mode values `0...3` and add:

```text
cave-batch <side-0-1> <updates> <interval-us>
```

For update `n`, use MIDI channel `14 + side`, voice `n % 9`, and:

```swift
let caveFrequencies = [16, 15999, 440, 440, 8000, 31, 1760, 55, 12000]
```

Send LSB CC `55 + voice` first, then MSB CC `23 + voice`; sleep once after both. Print
side, update count, CC count, and elapsed duration. Verify GREEN:

Replace the original unconditional channel guard with:

```swift
guard let address = Int(arguments[1]) else { usage() }
let channel: Int
if mode == "cave-batch" {
    guard (0...1).contains(address) else { usage() }
    channel = 14 + address
} else {
    guard (1...16).contains(address) else { usage() }
    channel = address
}
```

```bash
swift Tools/midi_stress.swift --help 2>&1 | rg 'cave-batch'
```

- [ ] **Step 7: Commit the reusable infrastructure and sender**

```bash
rm -rf tests/dsp/__pycache__ tests/esp32/__pycache__ \
  tests/esp32/wingie2_product_benchmark/__pycache__
git add Tools/midi_diag_session.py Tools/midi_stress.swift \
  tests/dsp/extract_faust_user.py tests/dsp/extract_faust_user_test.py \
  tests/esp32/check_deadline.py tests/esp32/check_deadline_test.py \
  tests/esp32/wingie2_generated_benchmark \
  tests/esp32/wingie2_product_benchmark
git diff --cached --check
git commit -m "test: 复用 modal bank 测量基础设施"
```

### Task 2: Build the Analyzer Red/Green Contract

**Files:**
- Create: `tests/dsp/analyze_modal_bank_test.py`
- Create: `tests/dsp/analyze_modal_bank.py`

- [ ] **Step 1: Write the failing analyzer test**

Create `tests/dsp/analyze_modal_bank_test.py`:

```python
#!/usr/bin/env python3
import importlib.util
import pathlib
import numpy as np

MODULE_PATH = pathlib.Path(__file__).with_name("analyze_modal_bank.py")

def load_module():
    spec = importlib.util.spec_from_file_location("modal_bank", MODULE_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def damped_sine(frequency, decay, samples=524288, sample_rate=44100):
    n = np.arange(samples, dtype=np.float64)
    rho = 0.001 ** (1.0 / (decay * sample_rate))
    return (rho ** n) * np.sin(2.0 * np.pi * frequency * n / sample_rate)

def main():
    module = load_module()
    clean = damped_sine(440.0, 10.0)
    result = module.analyze_single_peak(clean, 44100, 440.0)
    assert result["frequency_error_cents"] <= 5.0, result
    assert result["significant_peak_count"] == 1, result

    quiet_secondary = clean + 0.05 * damped_sine(880.0, 10.0)
    assert module.analyze_single_peak(
        quiet_secondary, 44100, 440.0)["significant_peak_count"] == 1

    loud_secondary = clean + 0.30 * damped_sine(880.0, 10.0)
    try:
        module.analyze_single_peak(loud_secondary, 44100, 440.0)
    except ValueError as error:
        assert "secondary peak" in str(error)
    else:
        raise AssertionError("-10.5 dB secondary peak unexpectedly passed")

    state = np.exp(-np.arange(8192, dtype=np.float64) / 1000.0)
    output = state * np.sin(2.0 * np.pi * 440.0 * np.arange(8192) / 44100.0)
    contraction = module.validate_contraction(state, output)
    assert contraction["active_tail"] is True, contraction
    growing = state.copy()
    growing[4096] = growing[4095] + 0.01
    try:
        module.validate_contraction(growing, output)
    except ValueError as error:
        assert "state energy grew" in str(error)
    else:
        raise AssertionError("growing state unexpectedly passed")

if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Verify RED**

```bash
python3 tests/dsp/analyze_modal_bank_test.py
```

Expected: FAIL because `analyze_modal_bank.py` does not exist.

- [ ] **Step 3: Implement the analyzer with fixed constants**

Create `tests/dsp/analyze_modal_bank.py`:

```python
#!/usr/bin/env python3
import math
import numpy as np
from scipy.signal import find_peaks

MAX_FREQUENCY_ERROR_CENTS = 5.0
SECONDARY_PEAK_DB = -18.0
MIN_PROMINENCE_DB = 6.0
MAX_ABS_OUTPUT = 8.0
GROWTH_TOLERANCE = 1e-5
ACTIVE_THRESHOLD = 1e-9

def estimate_pole_frequency(samples, sample_rate):
    samples = np.asarray(samples, dtype=np.float64)
    segment = samples[2048:]
    peak = float(np.max(np.abs(segment)))
    if not np.isfinite(segment).all() or peak <= ACTIVE_THRESHOLD:
        raise ValueError("inactive or non-finite modal tail")
    segment = segment / peak
    target = segment[2:]
    predictors = np.column_stack((segment[1:-1], segment[:-2]))
    coefficients, _, _, _ = np.linalg.lstsq(predictors, target, rcond=None)
    a, b = float(coefficients[0]), float(coefficients[1])
    if b >= 0.0:
        raise ValueError("tail is not a damped second-order recurrence")
    radius = math.sqrt(-b)
    cosine = a / (2.0 * radius)
    if not -1.0 <= cosine <= 1.0:
        raise ValueError("pole angle is out of range")
    return math.acos(cosine) * sample_rate / (2.0 * math.pi)

def analyze_single_peak(samples, sample_rate, target_frequency):
    samples = np.asarray(samples, dtype=np.float64)
    if samples.size != 524288 or not np.isfinite(samples).all():
        raise ValueError("peak render must contain 524288 finite samples")
    estimated = estimate_pole_frequency(samples, sample_rate)
    error_cents = abs(1200.0 * math.log2(estimated / target_frequency))
    magnitude = np.abs(np.fft.rfft(samples))
    db = 20.0 * np.log10(np.maximum(magnitude, 1e-12) / np.max(magnitude))
    peaks, _ = find_peaks(db, prominence=MIN_PROMINENCE_DB)
    edge_peaks = []
    if db[0] > db[1]:
        edge_peaks.append(0)
    if db[-1] > db[-2]:
        edge_peaks.append(db.size - 1)
    peaks = np.unique(np.concatenate((peaks, np.asarray(edge_peaks, dtype=int))))
    significant = peaks[db[peaks] >= SECONDARY_PEAK_DB]
    if significant.size != 1:
        raise ValueError(f"secondary peak gate failed: {significant.size} significant peaks")
    if error_cents > MAX_FREQUENCY_ERROR_CENTS:
        raise ValueError(f"pole frequency error {error_cents} cents")
    return {"estimated_frequency": estimated,
            "frequency_error_cents": error_cents,
            "significant_peak_count": int(significant.size),
            "primary_peak_db": float(db[significant[0]])}

def validate_contraction(state_norm, output):
    state = np.asarray(state_norm, dtype=np.float64)
    output = np.asarray(output, dtype=np.float64)
    if not np.isfinite(state).all() or not np.isfinite(output).all():
        raise ValueError("non-finite state or output")
    if float(np.max(np.abs(output))) > MAX_ABS_OUTPUT:
        raise ValueError("output exceeded bound")
    growth = float(np.max(np.diff(state)))
    if growth > GROWTH_TOLERANCE:
        raise ValueError("state energy grew during zero input")
    tail = output[-128:]
    active_tail = bool(np.sqrt(np.mean(tail * tail)) > ACTIVE_THRESHOLD)
    if not active_tail:
        raise ValueError("tail output is silent")
    return {"max_state_norm": float(np.max(state)),
            "max_state_growth": growth, "active_tail": active_tail}
```

- [ ] **Step 4: Verify GREEN and commit**

```bash
python3 tests/dsp/analyze_modal_bank_test.py
rm -rf tests/dsp/__pycache__
git add tests/dsp/analyze_modal_bank.py tests/dsp/analyze_modal_bank_test.py
git diff --cached --check
git commit -m "test: 固定 modal bank host 门禁"
```

### Task 3: Add the Isolated Held-Coefficient Contract

**Files:**
- Create: `tests/dsp/block_rate_modal_bank_contract.dsp`
- Create: `tests/dsp/render_block_rate_modal_bank.cpp`

- [ ] **Step 1: Add the Faust contract**

Create `tests/dsp/block_rate_modal_bank_contract.dsp`:

```faust
import("stdfaust.lib");

blockSize = 32;
blockPulse = ba.time % blockSize == 0;
blockHold(x) = x : control(blockPulse) : ba.sAndH(blockPulse);
freq = hslider("freq", 440, 16, 16000, 0.01);
t60 = hslider("t60", 5, 0.1, 10, 0.01) : si.smoo;

rhoReference = pow(0.001, 1.0 / (t60 * ma.SR));
rho = blockHold(rhoReference);
theta = 2 * ma.PI * freq / ma.SR;
c = blockHold(cos(theta));
s = blockHold(sin(theta));

rotation(r, sine, cosine, x) =
    ((_<:_,_), (_<:_,_)
      : (*(sine), *(cosine), *(cosine), *(0-sine))
      :> (*(r), (*(r) : +(x)))) ~ cross
with { cross = _,_ <: !,_,_,!; };

states = rotation(rho, s, c);
rhoProbe = _ * 0 + rho;
sProbe = _ * 0 + s;
cProbe = _ * 0 + c;
process = _ <: states, rhoProbe, sProbe, cProbe;
```

- [ ] **Step 2: Add the deterministic renderer**

Restore and rename the archived Faust host renderer:

```bash
git restore --source=archive/coupled-mode-filter -- \
  tests/dsp/render_coupled_mode_filter.cpp
git mv tests/dsp/render_coupled_mode_filter.cpp \
  tests/dsp/render_block_rate_modal_bank.cpp
```

Replace its CLI, schedule, and output loop with this exact contract:

```cpp
// CLI:
// render_block_rate_modal_bank <boundary|duplicates|crossing|reverse|ratio-jump>
//                              <output.f32>
// Initialize mydsp at 44100 Hz through MapUI.
// Inject one unit impulse at sample 0, then zero input for 8192 samples.
static const float kBoundary[] = {16.0f, 16000.0f, 16.0f, 16000.0f};
static const float kDuplicates[] = {440.0f, 440.0f, 440.0f, 440.0f};
static const float kCrossing[] = {220.0f, 1760.0f, 330.0f, 1320.0f};
static const float kReverse[] = {8000.0f, 2000.0f, 500.0f, 125.0f};
static const float kRatioJump[] = {110.0f, 15999.0f, 16.0f, 7040.0f};
// Change freq at samples 32, 64, 96, 128; never call clear/init again.
// Write seven interleaved float32 columns:
// frequency, hypot(p,q), (2/rho)*q, finite, rho, sin, cos.
// Fail if output is non-finite or rho/sin/cos changes inside a block.
```

- [ ] **Step 3: Compile and exercise every scenario**

```bash
faust -a tests/dsp/render_block_rate_modal_bank.cpp \
  -I "$FAUST_ROOT/share/faust" \
  tests/dsp/block_rate_modal_bank_contract.dsp \
  -o /tmp/wingie2-modal-bank-contract.cpp
c++ -std=c++11 -O2 -I "$FAUST_ROOT/include" \
  /tmp/wingie2-modal-bank-contract.cpp -o /tmp/wingie2-modal-bank-contract
for scenario in boundary duplicates crossing reverse ratio-jump; do
  /tmp/wingie2-modal-bank-contract "$scenario" \
    "/tmp/wingie2-modal-bank-$scenario.f32"
done
```

Load every seven-column file and call
`validate_contraction(data[:,1], data[:,2])`. Expected: all pass and each 32-sample slice
has constant rho/sin/cos probes.

- [ ] **Step 4: Inspect and commit**

```bash
rg -n 'std::(pow|sin|cos)|fRec.*\[0\] =' /tmp/wingie2-modal-bank-contract.cpp
git add tests/dsp/block_rate_modal_bank_contract.dsp \
  tests/dsp/render_block_rate_modal_bank.cpp
git diff --cached --check
git commit -m "test: 添加 block-rate rotation 隔离合同"
```

Expected: paired assignments share held rho; no reset occurs on frequency changes.

### Task 4: Add the Generated-Structure Gate

**Files:**
- Create: `tests/dsp/check_modal_bank_generated.py`
- Create: `tests/dsp/check_modal_bank_generated_test.py`

- [ ] **Step 1: Restore, rename, and replace the checker fixture**

```bash
git restore --source=archive/coupled-mode-filter -- \
  tests/dsp/check_coupled_generated.py tests/dsp/check_coupled_generated_test.py
git mv tests/dsp/check_coupled_generated.py tests/dsp/check_modal_bank_generated.py
git mv tests/dsp/check_coupled_generated_test.py \
  tests/dsp/check_modal_bank_generated_test.py
```

The passing one-mode/one-channel fixture contains one modulo-32 guarded `pow`, `sin`, and
`cos`, plus two rho-scaled state assignments. Assert exactly:

```python
assert module.analyze(GUARDED, expected_modes=1, expected_channels=1) == {
    "block_modulus": 32,
    "decay_pow_calls": 1,
    "guarded_decay_pow_calls": 1,
    "sin_calls": 1,
    "guarded_sin_calls": 1,
    "cos_calls": 1,
    "guarded_cos_calls": 1,
    "rho_scaled_state_updates": 2,
    "tf2np_calls": 0,
    "nlf2_calls": 0,
    "sqrt_calls": 0,
}
```

Add rejection fixtures for unguarded `pow/sin/cos`, one missing state update, `sqrt`,
`tf2np`, `nlf2`, wrong expected count, and incomplete user markers.

- [ ] **Step 2: Verify RED**

```bash
python3 tests/dsp/check_modal_bank_generated_test.py
```

Expected: FAIL because the restored checker lacks trig fields and expected-count arguments.

- [ ] **Step 3: Implement the checker**

Retain brace matching and user-section extraction. Add:

```python
SIN_PATTERN = re.compile(r"\b(?:std::)?sinf?\s*\(")
COS_PATTERN = re.compile(r"\b(?:std::)?cosf?\s*\(")

def guarded_calls(body, pattern, loops, conditions, label):
    calls = [match.start() for match in pattern.finditer(body)]
    for call in calls:
        if not any(start < call < end for start, end in loops):
            raise ValueError(f"{label} is outside generated sample loop")
        if not any(start < call < end for start, end in conditions):
            raise ValueError(f"unguarded {label} remains in generated sample loop")
    return len(calls)
```

`analyze(text, expected_modes, expected_channels)` requires exactly `expected_channels`
guarded decay `pow`, `expected_modes` guarded `sin` and `cos`, `expected_modes * 2`
rho-scaled state updates, modulo 32, and zero `sqrt/tf2np/nlf2`. CLI:

```text
check_modal_bank_generated.py GENERATED_CPP --expected-modes 18
                              --expected-channels 2 --output-json FILE
```

- [ ] **Step 4: Verify GREEN and commit**

```bash
python3 tests/dsp/check_modal_bank_generated_test.py
rm -rf tests/dsp/__pycache__
git add tests/dsp/check_modal_bank_generated.py \
  tests/dsp/check_modal_bank_generated_test.py
git diff --cached --check
git commit -m "test: 固定 modal bank 生成结构门禁"
```

### Task 5: Add Full-Product Host Gates

**Files:**
- Create: `tests/dsp/render_generated_wingie_modal.cpp`
- Create: `tests/dsp/analyze_generated_wingie_modal.py`
- Create: `tests/dsp/analyze_generated_wingie_modal_test.py`

- [ ] **Step 1: Restore and rename the archived gate**

```bash
git restore --source=archive/coupled-mode-filter -- \
  tests/dsp/render_generated_wingie_coupled.cpp \
  tests/dsp/analyze_generated_coupled.py \
  tests/dsp/analyze_generated_coupled_test.py
git mv tests/dsp/render_generated_wingie_coupled.cpp \
  tests/dsp/render_generated_wingie_modal.cpp
git mv tests/dsp/analyze_generated_coupled.py \
  tests/dsp/analyze_generated_wingie_modal.py
git mv tests/dsp/analyze_generated_coupled_test.py \
  tests/dsp/analyze_generated_wingie_modal_test.py
```

- [ ] **Step 2: Write analyzer RED cases**

Extend the renamed test with:

```python
assert module.compare_reference(reference, reference.copy(),
                                "rapid-notes", "standard", 0)["pass"]
for target in (16.0, 440.0, 8000.0, 16000.0):
    samples = damped_sine(target, 10.0)
    result = module.analyze_single_peak(samples, module.SAMPLE_RATE, target)
    assert result["frequency_error_cents"] <= 5.0
try:
    module.analyze_single_peak(
        damped_sine(440.0, 10.0) + 0.30 * damped_sine(880.0, 10.0),
        module.SAMPLE_RATE, 440.0)
except ValueError as error:
    assert "secondary peak" in str(error)
else:
    raise AssertionError("two-peak fixture unexpectedly passed")
```

Copy `damped_sine` from Task 2. Run now; expect RED because the renamed analyzer lacks the
new single-peak API. Then import it explicitly in the implementation:

```python
from analyze_modal_bank import analyze_single_peak, estimate_pole_frequency
```

- [ ] **Step 3: Extend the renderer with exact scenarios**

Keep archived `decay-step`, `mode-step`, `slow-notes`, `rapid-notes`, and `static`. Add:

```text
render_generated_wingie_modal frequency-jump <standard|alternate> OUTPUT
render_generated_wingie_modal mute-state <standard|alternate> OUTPUT
render_generated_wingie_modal peak <channel-0-1> <mode-index-0-8>
                              <target-hz> OUTPUT
```

For `frequency-jump`, set mode `3` (Cave) and all left/right `cave_freq_0..8` every block:

```cpp
constexpr float kFrequencySets[5][9] = {
  {16, 55, 110, 220, 440, 880, 1760, 3520, 16000},
  {440, 440, 440, 440, 440, 440, 440, 440, 440},
  {8000, 4000, 2000, 1000, 500, 250, 125, 62.5f, 31.25f},
  {220, 1760, 330, 1320, 440, 990, 550, 660, 16},
  {16000, 16, 15999, 17, 12000, 23, 7040, 31, 3520},
};
```

For `mute-state`, impulse at block 0, direct-mute mode index 4 for the first half, unmute at
the midpoint, and require active tail; never clear DSP. For `peak`, use Decay 10, Cave mode,
one direct output, a `0.001` impulse, and exactly `524288` two-channel samples. Reject target
outside `[16,16000]`.

- [ ] **Step 4: Extend the analyzer matrix**

Run tracked/reference parity only for the historical four scenarios. Run candidate
finite/bounded/active checks for all six scenarios and both tunings. Add:

```python
MODES = (0, 1, 2)
MODE_INDICES = (0, 4, 8)
NOTES = (24, 36, 60, 84, 96)
DECAYS = (0.1, 5.0, 10.0)
CAVE_TARGETS = (16.0, 50.0, 440.0, 8000.0, 16000.0)
PEAK_TARGETS = (16.0, 440.0, 8000.0, 16000.0)
```

Use recurrence estimation for every static row and the fixed FFT gate for every peak target,
both channels, and indices 0/4/8. Require candidate output at `16000 Hz` active; remove the
archived cap-mute skip.

- [ ] **Step 5: Verify and commit**

```bash
python3 tests/dsp/analyze_generated_wingie_modal_test.py
rm -rf tests/dsp/__pycache__
git add tests/dsp/render_generated_wingie_modal.cpp \
  tests/dsp/analyze_generated_wingie_modal.py \
  tests/dsp/analyze_generated_wingie_modal_test.py
git diff --cached --check
git commit -m "test: 添加 modal bank 产品 host 门禁"
```

### Task 6: Generate the Candidate Outside Git

**Files:**
- Read: `Wingie2.dsp`
- Create outside repository: `/tmp/wingie2-modal-bank-candidate/`

- [ ] **Step 1: Copy product source outside the repository**

```bash
rm -rf /tmp/wingie2-modal-bank-candidate
mkdir -p /tmp/wingie2-modal-bank-candidate/Wingie2
cp Wingie2.dsp /tmp/wingie2-modal-bank-candidate/Wingie2.dsp
```

- [ ] **Step 2: Apply the candidate helper outside Git**

Immediately after `nHarmonics = 9;`, add:

```faust
modalBlockSize = 32;
modalBlockPulse = ba.time % modalBlockSize == 0;
modalBlockHold(x) = x : control(modalBlockPulse) : ba.sAndH(modalBlockPulse);
```

Immediately before `r(note, index, source)`, add:

```faust
modalRotation(freq, rho, x) =
    ((_<:_,_), (_<:_,_) : par(i, 4, _ <: _,_)
      : (*(s), *(c), *(c), *(0-s))
      :> (*(rho), (*(rho) : +(x)))) ~ cross
with {
  theta = 2 * ma.PI * freq / ma.SR;
  c = cos(theta) : modalBlockHold;
  s = sin(theta) : modalBlockHold;
  cross = _,_ <: !,_,_,!;
};

blockRateMode(freq, t60, gain) = modalRotation(freq, rho) : _,!
    : *(scale) * gain
    : attach(_, t60)
with {
  rhoReference = pow(0.001, 1.0 / (t60 * ma.SR));
  rho = rhoReference : modalBlockHold;
  scale = (2.0 / rhoReference) : modalBlockHold;
};
```

Replace `r()` with:

```faust
r(note, index, source) = blockRateMode(a, b, ba.lin2LogGain(c))
with {
  a = max(16, min(f(note, index, source), 16000));
  b = decay;
  c = env_mute(button("mute_%index"));
};
```

This removes the historical `a == 16000` automatic mute and its `si.smoo`. It also removes
the old mode-change envelope from T60 preparation, as approved by the spec; the existing
mode-change envelope remains in the external output chain. Do not change frequency mapping,
the Decay control, input conditioning, wet/dry, or output processing.

- [ ] **Step 3: Generate and record identity**

```bash
cd /tmp/wingie2-modal-bank-candidate
test "$(command -v faust)" = "$FAUST_ROOT/bin/faust"
faust2esp32 -ac101 -lib Wingie2.dsp
unzip -q Wingie2.zip
test -s Wingie2/Wingie2.cpp
test -s Wingie2/Wingie2.h
shasum -a 256 "$FAUST_ROOT/bin/faust" \
  "$FAUST_ROOT/share/faust/filters.lib" \
  "$FAUST_ROOT/share/faust/oscillators.lib" \
  Wingie2.dsp Wingie2/Wingie2.cpp Wingie2/Wingie2.h Wingie2.zip \
  > artifact.sha256
shasum -a 256 --check artifact.sha256
```

### Task 7: Run Generated and Full-Product Host Gates

**Files:**
- Read outside repository: `/tmp/wingie2-modal-bank-candidate/`
- Create outside repository: `/tmp/wingie2-modal-bank-host/`

- [ ] **Step 1: Run the generated structure checker**

```bash
python3 tests/dsp/check_modal_bank_generated.py \
  /tmp/wingie2-modal-bank-candidate/Wingie2/Wingie2.cpp \
  --expected-modes 18 --expected-channels 2 \
  --output-json /tmp/wingie2-modal-bank-structure.json
```

Expected: modulo 32, 2 guarded decay `pow`, 18 guarded `sin`, 18 guarded `cos`, 36
rho-scaled state updates, and zero `sqrt/tf2np/nlf2`.

- [ ] **Step 2: Build all three renderers**

Generate a pinned reference and extract all headers:

```bash
rm -rf /tmp/wingie2-modal-bank-reference
mkdir -p /tmp/wingie2-modal-bank-reference
cp Wingie2.dsp /tmp/wingie2-modal-bank-reference/Wingie2.dsp
cd /tmp/wingie2-modal-bank-reference
faust2esp32 -ac101 -lib Wingie2.dsp
unzip -q Wingie2.zip
cd -
python3 tests/dsp/extract_faust_user.py Wingie2/Wingie2.cpp \
  /tmp/wingie2-modal-bank-tracked-user.h
python3 tests/dsp/extract_faust_user.py \
  /tmp/wingie2-modal-bank-reference/Wingie2/Wingie2.cpp \
  /tmp/wingie2-modal-bank-reference-user.h
python3 tests/dsp/extract_faust_user.py \
  /tmp/wingie2-modal-bank-candidate/Wingie2/Wingie2.cpp \
  /tmp/wingie2-modal-bank-candidate-user.h
```

Then compile:

```bash
for artifact in tracked reference candidate; do
  c++ -std=c++11 -O2 -I "$FAUST_ROOT/include" \
    -DGENERATED_DSP_HEADER='"/tmp/wingie2-modal-bank-'"$artifact"'-user.h"' \
    tests/dsp/render_generated_wingie_modal.cpp \
    -o "/tmp/wingie2-modal-bank-$artifact-renderer"
done
```

Expected: all compile. The only accepted warning is Faust 2.59.6 `PathBuilder.h` using
deprecated `sprintf` on macOS.

- [ ] **Step 3: Run the complete analyzer**

```bash
python3 tests/dsp/analyze_generated_wingie_modal.py \
  /tmp/wingie2-modal-bank-tracked-renderer \
  /tmp/wingie2-modal-bank-reference-renderer \
  /tmp/wingie2-modal-bank-candidate-renderer \
  /tmp/wingie2-modal-bank-host.json
```

Expected: zero failures across baseline parity, six dynamic scenarios, static pole matrices,
16/16000 Hz behavior, mute-state continuity, and all single-peak renders.

- [ ] **Step 4: Commit only host gate infrastructure**

```bash
rm -rf tests/dsp/__pycache__
git add tests/dsp
git diff --cached --check
git commit -m "test: 固定 block-rate modal bank host 结果"
```

If any Task 7 gate fails, stop and proceed only to Task 11. Do not design another algorithm
inside this plan.

### Task 8: Measure ESP32 Deadline

**Files:**
- Modify: `tests/esp32/wingie2_generated_benchmark/benchmark.cpp`
- Reuse: `tests/esp32/wingie2_product_benchmark/*`
- Create outside repository: `/tmp/wingie2-modal-bank-benchmarks/`

- [ ] **Step 1: Change generated benchmark stimulus to Cave frequencies**

Replace `setPitch()` with `setControls(block)`. It must still set `note0/note1` every block,
set both modes to Cave (`3`) during setup, and set all 18 Cave sliders from this matrix:

```cpp
constexpr float kFrequencySets[5][9] = {
  {16, 55, 110, 220, 440, 880, 1760, 3520, 16000},
  {440, 440, 440, 440, 440, 440, 440, 440, 440},
  {8000, 4000, 2000, 1000, 500, 250, 125, 62.5f, 31.25f},
  {220, 1760, 330, 1320, 440, 990, 550, 660, 16},
  {16000, 16, 15999, 17, 12000, 23, 7040, 31, 3520},
};
```

Use `block % 5` and paths `/Wingie/left/cave_freq_i` and
`/Wingie/right/cave_freq_i`. Keep non-zero deterministic input and the timed region unchanged.

- [ ] **Step 2: Build the generated-class benchmark**

```bash
FAUST_ROOT="$FAUST_ROOT" \
  tests/esp32/wingie2_generated_benchmark/build_generated_benchmark.sh \
  /tmp/wingie2-modal-bank-candidate/Wingie2/Wingie2.cpp modal-bank
```

Record build size, image SHA-256, and valid `esptool.py image_info`.

- [ ] **Step 3: Measure generated class and restore baseline**

Flash app0 only, capture one 100,000-block result to
`/tmp/wingie2-modal-bank-benchmarks/generated.txt`, then run the full recovery protocol,
capture startup, and wait for explicit restored-wet confirmation.

```bash
python3 tests/esp32/check_deadline.py \
  /tmp/wingie2-modal-bank-benchmarks/generated.txt --expect pass \
  --output-json /tmp/wingie2-modal-bank-benchmarks/generated.json
```

If it fails, stop and proceed only to Task 11.

- [ ] **Step 4: Build the end-to-end benchmark**

```bash
FAUST_ROOT="$FAUST_ROOT" \
  tests/esp32/wingie2_product_benchmark/build_product_benchmark.sh \
  /tmp/wingie2-modal-bank-candidate/Wingie2/Wingie2.cpp modal-bank
```

Record size, image SHA-256, reinjected user-section identity, instrumented architecture hash,
and valid image metadata.

- [ ] **Step 5: Measure end-to-end String and Cave captures**

Run two separate candidate stages. During the first recorder window send 40,000 rapid String
Note On/Off pairs. During the second send 40,000 Cave frequency updates. Never send both
stimuli simultaneously. Restore/verify/readback historical app0 and obtain explicit restored
wet confirmation after each stage. Save the slower distribution as `product-worst.txt`.

Use these host commands during the corresponding recorder windows:

```bash
swift Tools/midi_stress.swift config 1 1 16383
swift Tools/midi_stress.swift batch 1 40000 1000
swift Tools/midi_stress.swift config 1 3 16383
swift Tools/midi_stress.swift cave-batch 0 40000 1000
```

```bash
python3 tests/esp32/check_deadline.py \
  /tmp/wingie2-modal-bank-benchmarks/product-worst.txt --expect pass \
  --output-json /tmp/wingie2-modal-bank-benchmarks/product-worst.json
```

If p99 fails, stop. The next work is a new normalized-lattice design, not another task here.

- [ ] **Step 6: Commit benchmark changes**

```bash
rm -rf tests/esp32/__pycache__ \
  tests/esp32/wingie2_product_benchmark/__pycache__
git add tests/esp32
git diff --cached --check
git commit -m "test: 固定 modal bank ESP32 deadline 门禁"
```

### Task 9: Build Diagnostic Firmware

**Files:**
- Modify: `Wingie2/midi_diagnostics.ino`
- Create outside repository: `/tmp/wingie2-modal-bank-firmware/`

- [ ] **Step 1: Expose Cave values only in diagnostic snapshots**

Verify RED first:

```bash
rg -n 'MIDI_DIAG cave_ch=' Wingie2/midi_diagnostics.ino
```

Expected: FAIL. Then, inside `#if MIDI_DIAGNOSTICS`, extend the snapshot function:

```cpp
for (int ch = 0; ch < 2; ch++) {
  Serial.printf("MIDI_DIAG cave_ch=%d", ch);
  for (int voice = 0; voice < 9; voice++) {
    char path[48];
    snprintf(path, sizeof(path), ch == 0
        ? "/Wingie/left/cave_freq_%d" : "/Wingie/right/cave_freq_%d", voice);
    Serial.printf(" %.1f", dsp.getParamValue(path));
  }
  Serial.println();
}
```

Normal firmware is unchanged because this is compiled out when diagnostics are disabled.

- [ ] **Step 2: Build normal and diagnostic candidate firmware outside Git**

Copy current `Wingie2` and `Libraries` to `/tmp/wingie2-modal-bank-firmware/src`, replace
only DSP/C++/header with frozen candidate artifacts, then:

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 \
  --libraries /tmp/wingie2-modal-bank-firmware/src/Libraries \
  --output-dir /tmp/wingie2-modal-bank-firmware/normal \
  /tmp/wingie2-modal-bank-firmware/src/Wingie2
arduino-cli compile --fqbn esp32:esp32:esp32 \
  --libraries /tmp/wingie2-modal-bank-firmware/src/Libraries \
  --build-property compiler.cpp.extra_flags=-DMIDI_DIAGNOSTICS=1 \
  --output-dir /tmp/wingie2-modal-bank-firmware/diag \
  /tmp/wingie2-modal-bank-firmware/src/Wingie2
for image in /tmp/wingie2-modal-bank-firmware/{normal,diag}/*.bin; do
  /tmp/wingie2-esptool-venv/bin/esptool.py --chip esp32 image_info "$image" \
    | rg 'Checksum:.*valid|Validation Hash:.*valid'
done
```

Record size and hashes.

- [ ] **Step 3: Commit diagnostic-only changes**

```bash
git add Wingie2/midi_diagnostics.ino
git diff --cached --check
git commit -m "test: 添加 Cave 实时频率压力工具"
```

### Task 10: Run Hardware Pressure and User A/B

**Files:**
- Read/write hardware: `/dev/cu.usbserial-11310`, app0 `0x10000` only
- Create outside repository: `/tmp/wingie2-modal-bank-pressure.txt`

- [ ] **Step 1: Flash diagnostic candidate and open one persistent session**

Use the Global Hardware Recovery Protocol and start one
`Tools/midi_diag_session.py | tee /tmp/wingie2-modal-bank-pressure.txt` process. Do not reopen
serial between reset/snapshot commands or batches.

- [ ] **Step 2: Run String pressure for both channels**

For channels 1 and 2: configure String/Decay 10, reset counters, send 100,000 variable-pitch
Note On/Off pairs, snapshot, send a low-rate marker, and snapshot again. Require exact parsed
and callback counts, zero errors, marker update, no reset/stall, and explicit confirmation of
target wet, other wet, and dry/thru. Stop at first failure.

- [ ] **Step 3: Run Poly and Bar pressure**

For each mode and channel, send 10,000 pairs with the same counter, marker, reset/stall, and
three-path audio requirements. Record each row separately.

- [ ] **Step 4: Run future-ratio-equivalent Cave pressure**

For side 0 then side 1: set that side to Cave, reset diagnostics, then:

```bash
swift Tools/midi_stress.swift config "$((side + 1))" 3 16383
# Send `r` through the already-open persistent diagnostic session here.
swift Tools/midi_stress.swift cave-batch "$side" 100000 1000
```

Require parsed count to increase by exactly 200,000 CC messages, zero errors, final Cave
values matching the deterministic last nine updates, no reset/stall, and explicit
confirmation of target wet, other wet, and dry/thru. Mute/unmute one mode after the batch and
ask whether its continuing-tail behavior is normal.

- [ ] **Step 5: Restore historical app0**

Close the persistent session, execute complete recovery, capture startup, and wait for
explicit confirmation that historical left and right wet are normal.

- [ ] **Step 6: Perform controlled baseline/candidate A/B**

Only after every pressure row passes, compare historical baseline and normal candidate with
identical panel, input, and monitoring. Cover Poly/String/Bar/Cave, isolated-mode pitch,
Decay, gain, dynamics, slow/rapid notes, arbitrary Cave jumps, 16 kHz cap audibility,
mute/unmute, dry/thru, and both wet paths. Restore/verify/readback historical app0 after every
candidate exposure. Record exact user answers; numerical gates do not substitute.

### Task 11: Install an Accepted Candidate or Record Rejection

**Files:**
- Conditionally modify: `Wingie2.dsp`, `Wingie2/Wingie2.cpp`, `Wingie2/Wingie2.h`
- Create: `docs/superpowers/results/2026-07-11-block-rate-modal-bank-results.md`

- [ ] **Step 1: Apply the decision gate**

If any Task 7-10 gate failed, do not modify product DSP/generated files. Record
`candidate rejected`, the first failed gate, skipped stages, and whether an ESP32 deadline
failure authorizes a new normalized-lattice design session.

Only if every machine gate, pressure row, recovery, and user A/B passed, copy the frozen
candidate source/generated triplet into the product and verify hashes against its manifest.

- [ ] **Step 2: Verify an accepted product triplet**

For an accepted candidate only:

```bash
python3 tests/dsp/check_modal_bank_generated.py Wingie2/Wingie2.cpp \
  --expected-modes 18 --expected-channels 2 \
  --output-json /tmp/wingie2-modal-bank-final-structure.json
arduino-cli compile --fqbn esp32:esp32:esp32 --libraries Libraries \
  --output-dir /tmp/wingie2-modal-bank-final-normal Wingie2
arduino-cli compile --fqbn esp32:esp32:esp32 --libraries Libraries \
  --build-property compiler.cpp.extra_flags=-DMIDI_DIAGNOSTICS=1 \
  --output-dir /tmp/wingie2-modal-bank-final-diag Wingie2
git diff --check
```

Expected: structure and both builds pass; only intended product and diagnostic paths differ.

- [ ] **Step 3: Commit an accepted product separately**

```bash
git add Wingie2.dsp Wingie2/Wingie2.cpp Wingie2/Wingie2.h
git diff --cached --check
git commit -m "fix: 使用 block-rate modal bank"
```

Skip this step on rejection. Do not amend earlier commits.

- [ ] **Step 4: Write the result document**

Record tool hashes/versions, all source/generated/image hashes, every host row and peak
metric, generated counts, both ESP32 distributions, build sizes, every pressure row, exact
user A/B answers, every app0 write/verify, full readback comparison, startup state,
restored-wet confirmations, product decision, and confirmation that rejected plans were
untouched.

- [ ] **Step 5: Verify and commit results**

```bash
git diff --check
rg -n 'candidate (accepted|rejected)|block-rate|16 kHz|\+/-5 cents|ESP32|0x10000|0x140000|wet|TF2NP|coupled' \
  docs/superpowers/results/2026-07-11-block-rate-modal-bank-results.md
git add docs/superpowers/results/2026-07-11-block-rate-modal-bank-results.md
git diff --cached --check
git commit -m "test: 记录 block-rate modal bank 结果"
git status --short
```

- [ ] **Step 6: Preserve complete research history**

```bash
git tag -a archive/block-rate-modal-bank-$(date +%Y-%m-%d) \
  -m "Archive block-rate modal bank result" HEAD
```

Do not push, merge, delete the branch, or remove the worktree automatically. If rejected,
the finishing step preserves the branch/tag and puts only a concise result commit on `main`,
matching the TF2NP and coupled boundary.
