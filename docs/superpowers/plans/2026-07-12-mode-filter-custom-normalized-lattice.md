# Custom Normalized-Lattice Mode Filter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Evaluate Candidates E and R and, only if one passes every machine, hardware, and user-listening gate, replace Wingie2's rapidly retuned direct-form modal filters with that custom normalized-lattice candidate.

**Architecture:** Both candidates use the same two-state normalized lattice with channel-shared block-rate decay preparation and mode-specific block-rate reflection coefficients. Candidate E feeds a channel-shared `1-z^-2` difference into the lattice and normalizes its terminal tap for historical fixed-coefficient parity; Candidate R exposes the raw terminal tap. Candidate artifacts remain under `/tmp` until all gates pass, and no rejected TF2NP, coupled, or rotation candidate source is restored.

**Tech Stack:** Faust 2.59.6, C++11, Python 3 with NumPy/SciPy, ESP32 Arduino Core 2.0.4-cn, Xtensa cycle counter, Arduino CLI, CoreMIDI Swift, pyserial, esptool 3.3.

---

## Hard Boundary And Stop Rules

- Read first: `docs/superpowers/specs/2026-07-12-mode-filter-custom-normalized-lattice-design.md`.
- Execute in a new worktree and branch named `fix/custom-normalized-lattice`.
- Do not execute or edit the rejected TF2NP, coupled, or block-rate rotation plans.
- Generic measurement infrastructure may be selectively imported from
  `fix/block-rate-modal-bank`; candidate DSP and candidate-specific analyzers may not.
- Do not call `fi.tf2np`, `fi.nlf2`, or restore either rejected state recurrence.
- Keep Faust 2.59.6 and Core 2.0.4-cn fixed.
- Do not add firmware compensation, rate limiting, smoothing, reset, crossfade, fallback,
  automatic gain, or a new time constant.
- Preserve the historical mode-change envelope and its existing time constants.
- Clip each frequency to `[16, 16000] Hz`; both boundaries remain audible.
- User mute gates only direct output. Muted modes receive input and update both states.
- Before all machine and physical gates pass, do not modify `Wingie2.dsp`,
  `Wingie2/Wingie2.cpp`, or `Wingie2/Wingie2.h` in the worktree.
- Any failed gate stops that candidate. A generated-class failure skips product firmware.
- If both candidates fail, write the result, restore historical app0, archive the branch,
  and stop.
- Physical audio acceptance comes only from the user.

## Fixed Identities And Thresholds

```text
Faust compiler SHA-256   464d9ba249bea4b3589ad0ba5c7e8bf266b4f7e649523e68a022f73a164032c2
filters.lib SHA-256      6f82542b67778a5f46edc0fc29bf18b5b7d0337b43689f6f2dd07a08c6da45fa
historical Wingie2.dsp   fad94a49a0fe888aea854cdb3b02ab073a982909939db433431b609d1b6fbbd1
historical Wingie2.cpp   b9414abf86b17e42993479861ce81d3e70c56158351b13ab9d9ea1af195f6c39
historical Wingie2.h     525d057742d2894b281f87b072e73b6f52e3c83140385d52268bc7d5baaf5263
historical app0          6da9aca6795cc6ef9352705089c5c8aef58a91dc51d37c73592052ef2fa8c36b
sample rate              44100 Hz
block size               32 samples
ESP32 CPU                240 MHz
measured blocks          100000
p99 gate                 <= 580.5 us
max gate                 < 725.6 us
deadline misses          0
peak render              524288 samples
secondary peak floor     -18 dB
minimum prominence       6 dB
pitch error              <= +/-5 cents
Candidate E parity       <=1 cent, <=2% T60, <=0.25 dB peak gain
pre-nonlinearity output  <= 8.0 absolute
state-energy tolerance   1e-5 * max(1, energy)
active-tail RMS          > 1e-9
```

## File Structure

Selectively import generic files:

- `Tools/midi_diag_session.py` - persistent serial session.
- `Tools/midi_stress.swift` - deterministic String/Poly/Bar/Cave senders.
- `tests/dsp/extract_faust_user.py` and test - generated user-section extraction.
- `tests/esp32/check_deadline.py` and test - deadline parser.
- `tests/esp32/wingie2_generated_benchmark/*` - generated-class timing harness.
- `tests/esp32/wingie2_product_benchmark/build_product_benchmark.sh` - complete product
  build wrapper.
- `tests/esp32/wingie2_product_benchmark/inject.dsp` - architecture injection placeholder.

Create candidate-specific files:

- `tests/dsp/normalized_lattice_reference.py` - coefficient and recurrence oracle.
- `tests/dsp/normalized_lattice_reference_test.py` - coefficient, contraction, and exact
  factorization tests.
- `tests/dsp/make_normalized_lattice_candidates.py` and test - create immutable E/R product
  sources under `/tmp` from the historical DSP.
- `tests/dsp/check_normalized_lattice_generated.py` and test - UI, call placement, sharing,
  state, mute, and candidate-difference checker.
- `tests/dsp/custom_normalized_lattice_contract.dsp` - isolated E/R Faust contract.
- `tests/dsp/render_custom_normalized_lattice.cpp` - isolated coefficient/state renderer.
- `tests/dsp/analyze_custom_normalized_lattice.py` and test - contract and peak analysis.
- `tests/dsp/render_generated_wingie_lattice.cpp` - real-UI full-product renderer.
- `tests/dsp/analyze_generated_wingie_lattice.py` and test - complete dynamic/static matrix.
- `tests/esp32/wingie2_product_benchmark/instrument_segmented_esp32.py` and test - segmented
  input/compute/output/write/end-to-end cycle recorder.
- `docs/superpowers/results/2026-07-12-mode-filter-custom-normalized-lattice-results.md` -
  immutable evidence and final decision.

Only after explicit user acceptance, update together:

- `Wingie2.dsp`;
- `Wingie2/Wingie2.cpp`;
- `Wingie2/Wingie2.h`.

### Task 1: Create The Isolated Worktree And Freeze Recovery Anchors

**Files:**
- Read: `docs/superpowers/specs/2026-07-12-mode-filter-custom-normalized-lattice-design.md`
- Preserve outside repository: `/tmp/wingie2-lattice-baseline/`
- Preserve outside repository: `/tmp/wingie2-lattice-recovery-app0.bin`

- [ ] **Step 1: Create the worktree**

```bash
git status --short
git worktree add -b fix/custom-normalized-lattice \
  .worktrees/custom-normalized-lattice main
cd .worktrees/custom-normalized-lattice
test "$(git branch --show-current)" = fix/custom-normalized-lattice
git status --short
```

Expected: both status commands are empty and the branch assertion succeeds.

- [ ] **Step 2: Pin and verify Faust**

```bash
export FAUST_ROOT=/tmp/faust-2.59.6-mount/Faust-2.59.6/Faust-2.59.6
export PATH="$FAUST_ROOT/bin:$PATH"
export FAUSTARCH="$FAUST_ROOT/share/faust"
faust --version
shasum -a 256 "$FAUST_ROOT/bin/faust" "$FAUST_ROOT/share/faust/filters.lib"
```

Expected: Faust reports `2.59.6` and both hashes match the fixed identities above.

- [ ] **Step 3: Freeze the historical product**

```bash
rm -rf /tmp/wingie2-lattice-baseline
mkdir -p /tmp/wingie2-lattice-baseline/Wingie2
cp Wingie2.dsp /tmp/wingie2-lattice-baseline/Wingie2.dsp
cp Wingie2/Wingie2.cpp /tmp/wingie2-lattice-baseline/Wingie2/Wingie2.cpp
cp Wingie2/Wingie2.h /tmp/wingie2-lattice-baseline/Wingie2/Wingie2.h
shasum -a 256 /tmp/wingie2-lattice-baseline/Wingie2.dsp \
  /tmp/wingie2-lattice-baseline/Wingie2/Wingie2.cpp \
  /tmp/wingie2-lattice-baseline/Wingie2/Wingie2.h
chmod -R a-w /tmp/wingie2-lattice-baseline
```

Expected: the three hashes match the fixed historical identities.

- [ ] **Step 4: Build the unchanged baseline with Core 2.0.4-cn**

```bash
arduino-cli board details --fqbn esp32:esp32:esp32 | rg 'Board version:.*2\.0\.4'
rm -rf /tmp/wingie2-lattice-baseline-build
arduino-cli compile --fqbn esp32:esp32:esp32 --libraries Libraries \
  --output-dir /tmp/wingie2-lattice-baseline-build Wingie2
```

Expected: version assertion and compile succeed.

- [ ] **Step 5: Anchor the physical recovery image before any candidate write**

```bash
/tmp/wingie2-esptool-venv/bin/esptool.py --chip esp32 \
  --port /dev/cu.usbserial-11310 --no-stub read_flash --no-progress \
  0x10000 0x140000 /tmp/wingie2-lattice-recovery-app0.bin
test "$(stat -f %z /tmp/wingie2-lattice-recovery-app0.bin)" = 1310720
test "$(shasum -a 256 /tmp/wingie2-lattice-recovery-app0.bin | awk '{print $1}')" = \
  6da9aca6795cc6ef9352705089c5c8aef58a91dc51d37c73592052ef2fa8c36b
/tmp/wingie2-esptool-venv/bin/esptool.py --chip esp32 \
  image_info /tmp/wingie2-lattice-recovery-app0.bin
```

Expected: exact length, hash, and ESP32 image validation pass. Do not continue if the
device does not contain the historical image.

### Task 2: Import Only Generic Measurement Infrastructure

**Files:**
- Create from generic archive: `Tools/midi_diag_session.py`
- Modify from generic archive: `Tools/midi_stress.swift`
- Create from generic archive: `tests/dsp/extract_faust_user.py`
- Create from generic archive: `tests/dsp/extract_faust_user_test.py`
- Create from generic archive: `tests/esp32/check_deadline.py`
- Create from generic archive: `tests/esp32/check_deadline_test.py`
- Create from generic archive: `tests/esp32/wingie2_generated_benchmark/`
- Create from generic archive: `tests/esp32/wingie2_product_benchmark/build_product_benchmark.sh`
- Create from generic archive: `tests/esp32/wingie2_product_benchmark/inject.dsp`

- [ ] **Step 1: Selectively import the generic files**

```bash
git restore --source=fix/block-rate-modal-bank -- \
  Tools/midi_diag_session.py Tools/midi_stress.swift \
  tests/dsp/extract_faust_user.py tests/dsp/extract_faust_user_test.py \
  tests/esp32/check_deadline.py tests/esp32/check_deadline_test.py \
  tests/esp32/wingie2_generated_benchmark \
  tests/esp32/wingie2_product_benchmark/build_product_benchmark.sh \
  tests/esp32/wingie2_product_benchmark/inject.dsp
```

Expected: no modal-bank analyzer, candidate DSP, generated checker, or product
instrumentation is imported.

- [ ] **Step 2: Prove the imported boundary**

```bash
git diff --name-only | sort > /tmp/wingie2-lattice-imported.txt
cat > /tmp/wingie2-lattice-expected-import.txt <<'EOF'
Tools/midi_diag_session.py
Tools/midi_stress.swift
tests/dsp/extract_faust_user.py
tests/dsp/extract_faust_user_test.py
tests/esp32/check_deadline.py
tests/esp32/check_deadline_test.py
tests/esp32/wingie2_generated_benchmark/benchmark.cpp
tests/esp32/wingie2_generated_benchmark/build_generated_benchmark.sh
tests/esp32/wingie2_product_benchmark/build_product_benchmark.sh
tests/esp32/wingie2_product_benchmark/inject.dsp
EOF
diff -u /tmp/wingie2-lattice-expected-import.txt /tmp/wingie2-lattice-imported.txt
```

Expected: exact match.

- [ ] **Step 3: Run the generic tests**

```bash
python3 tests/dsp/extract_faust_user_test.py
python3 tests/esp32/check_deadline_test.py
swiftc -typecheck Tools/midi_stress.swift
python3 -m py_compile Tools/midi_diag_session.py
```

Expected: all exit zero.

- [ ] **Step 4: Commit the generic infrastructure**

```bash
git add Tools/midi_diag_session.py Tools/midi_stress.swift tests/dsp tests/esp32
git diff --cached --check
git commit -m "test: 复用 normalized-lattice 测量基础设施"
```

### Task 3: Build The Mathematical Oracle With TDD

**Files:**
- Create: `tests/dsp/normalized_lattice_reference.py`
- Create: `tests/dsp/normalized_lattice_reference_test.py`

- [ ] **Step 1: Write the failing coefficient and recurrence tests**

Create `tests/dsp/normalized_lattice_reference_test.py` with tests that import
`coefficients`, `step`, and `render` and assert:

```python
import math
import numpy as np
from scipy.signal import lfilter

from normalized_lattice_reference import coefficients, render, step

SR = 44100


def test_registered_worst_case_coefficients():
    c = coefficients(16.0, 10.05, SR)
    assert math.isclose(c.rho, 0.999984414207, rel_tol=0, abs_tol=5e-13)
    assert math.isclose(c.s1, -0.999997401560, rel_tol=0, abs_tol=5e-13)
    assert math.isclose(c.s2, 0.999968828657, rel_tol=0, abs_tol=5e-13)
    assert abs(c.s1) < 1.0 and abs(c.s2) < 1.0
    assert math.isclose(c.scale, 55557.102959, rel_tol=2e-9)


def test_zero_input_energy_identity_across_jump():
    z1, z2 = 0.25, -0.5
    for frequency in (16.0, 16000.0, 440.0, 8000.0):
        c = coefficients(frequency, 10.05, SR)
        raw, z1_next, z2_next = step(0.0, z1, z2, c)
        expected = z1 * z1 + c.s2 * c.s2 * z2 * z2
        assert math.isclose(z1_next*z1_next + z2_next*z2_next,
                            expected, rel_tol=0, abs_tol=1e-14)
        assert raw == z1_next
        z1, z2 = z1_next, z2_next


def test_candidate_e_matches_historical_held_filter():
    impulse = np.zeros(32768)
    impulse[0] = 1e-3
    for frequency in (16.0, 50.0, 440.0, 8000.0, 16000.0):
        for t60 in (0.05, 0.15, 5.0, 10.05):
            c = coefficients(frequency, t60, SR)
            expected = lfilter([1.0, 0.0, -1.0], [1.0, c.a1, c.a2], impulse)
            actual = render(impulse, c, candidate="E")
            np.testing.assert_allclose(actual, expected, rtol=2e-10, atol=2e-12)


def test_candidate_r_is_finite_and_deterministic():
    source = np.random.default_rng(0x57494E47).normal(0, 1e-3, 8192)
    c = coefficients(440.0, 10.05, SR)
    first = render(source, c, candidate="R")
    second = render(source, c, candidate="R")
    assert np.isfinite(first).all()
    np.testing.assert_array_equal(first, second)
```

- [ ] **Step 2: Run the tests to verify RED**

```bash
python3 tests/dsp/normalized_lattice_reference_test.py
```

Expected: FAIL because `normalized_lattice_reference` does not exist.

- [ ] **Step 3: Implement the oracle**

Create `tests/dsp/normalized_lattice_reference.py` with:

```python
from dataclasses import dataclass
import math
import numpy as np


@dataclass(frozen=True)
class Coefficients:
    rho: float
    a1: float
    a2: float
    s1: float
    s2: float
    c1: float
    c2: float
    scale: float


def coefficients(frequency, t60, sample_rate=44100):
    frequency = min(16000.0, max(16.0, float(frequency)))
    rho = 0.001 ** (1.0 / (float(t60) * sample_rate))
    a1 = -2.0 * rho * math.cos(2.0 * math.pi * frequency / sample_rate)
    a2 = rho * rho
    limit = 1.0 - np.finfo(np.float32).eps
    s2 = min(limit, max(-limit, a2))
    s1 = min(limit, max(-limit, a1 / (1.0 + a2)))
    c1 = math.sqrt(max(0.0, 1.0 - s1 * s1))
    c2 = math.sqrt(max(0.0, 1.0 - s2 * s2))
    return Coefficients(rho, a1, a2, s1, s2, c1, c2, 1.0/(c1*c2))


def step(value, z1, z2, c):
    t2 = c.c2 * value - c.s2 * z2
    t1 = c.c1 * t2 - c.s1 * z1
    return t1, t1, c.s1 * t2 + c.c1 * z1


def render(source, c, candidate):
    source = np.asarray(source, dtype=np.float64)
    output = np.zeros_like(source)
    z1 = z2 = 0.0
    delayed = [0.0, 0.0]
    for index, value in enumerate(source):
        lattice_input = value
        if candidate == "E":
            lattice_input = value - delayed[index & 1]
            delayed[index & 1] = value
        elif candidate != "R":
            raise ValueError(f"unknown candidate: {candidate}")
        raw, z1, z2 = step(lattice_input, z1, z2, c)
        output[index] = raw * (c.scale if candidate == "E" else 1.0)
    return output
```

- [ ] **Step 4: Run GREEN and commit**

```bash
python3 tests/dsp/normalized_lattice_reference_test.py
git add tests/dsp/normalized_lattice_reference.py \
  tests/dsp/normalized_lattice_reference_test.py
git commit -m "test: 固定 normalized-lattice 数学合同"
```

Expected: all assertions pass.

### Task 4: Generate Both Complete Product Candidates First

**Files:**
- Create: `tests/dsp/make_normalized_lattice_candidates.py`
- Create: `tests/dsp/make_normalized_lattice_candidates_test.py`
- Generate outside repository: `/tmp/wingie2-lattice-pair/{e,r}/`

- [ ] **Step 1: Write RED transformation tests**

The test must use a minimal historical-source fixture and require:

```python
def test_candidate_source_boundaries():
    for candidate in ("E", "R"):
        source = make_candidate(HISTORICAL_FIXTURE, candidate)
        assert "customNormalizedLattice" in source
        assert "fi.tf2np" not in source
        assert "fi.nlf2" not in source
        assert "env_mute" not in source
        assert "min(16000, max(16, f(note, i, mode)))" in source
        assert "channelRho((env_mode_change * decay) + 0.05)" in source
        assert "ba.lin2LogGain(1 - button(\"mute_%i\"))" in source
    assert "x - x''" in make_candidate(HISTORICAL_FIXTURE, "E")
    assert "1.0 / (c1 * c2)" in make_candidate(HISTORICAL_FIXTURE, "E")
    assert "x - x''" not in make_candidate(HISTORICAL_FIXTURE, "R")
    assert "1.0 / (c1 * c2)" not in make_candidate(HISTORICAL_FIXTURE, "R")
```

Run `python3 tests/dsp/make_normalized_lattice_candidates_test.py` and expect import failure.

- [ ] **Step 2: Implement exact anchored source transformation**

`make_candidate(source, candidate)` must reject any anchor count other than one. Insert the
following coefficient/state definition before historical `r(...)`:

```faust
latticeBlockSize = 32;
latticeBlockPulse = ba.time % latticeBlockSize == 0;
latticeHold(x) = x : control(latticeBlockPulse) : ba.sAndH(latticeBlockPulse);

customNormalizedLattice(s1, c1, s2, c2, x) = t1
letrec {
  'z1 = t1;
  'z2 = s1 * t2 + c1 * z1;
}
with {
  t2 = c2 * x - s2 * z2;
  t1 = c1 * t2 - s1 * z1;
};

latticeModeE(freq, rho, gain) = customNormalizedLattice(s1, c1, s2, c2, _) * scale * gain
with {
  a2 = rho * rho;
  a1 = -2 * rho * cos(2 * ma.PI * freq / ma.SR);
  smax = 1.0 - ma.EPSILON;
  s2 = max(-smax, min(smax, a2)) : latticeHold;
  s1 = max(-smax, min(smax, a1 / (1 + a2))) : latticeHold;
  c2 = sqrt(max(0, 1 - s2 * s2)) : latticeHold;
  c1 = sqrt(max(0, 1 - s1 * s1)) : latticeHold;
  scale = (1.0 / (c1 * c2)) : latticeHold;
};

latticeModeR(freq, rho, gain) = customNormalizedLattice(s1, c1, s2, c2, _) * gain
with {
  a2 = rho * rho;
  a1 = -2 * rho * cos(2 * ma.PI * freq / ma.SR);
  smax = 1.0 - ma.EPSILON;
  s2 = max(-smax, min(smax, a2)) : latticeHold;
  s1 = max(-smax, min(smax, a1 / (1 + a2))) : latticeHold;
  c2 = sqrt(max(0, 1 - s2 * s2)) : latticeHold;
  c1 = sqrt(max(0, 1 - s1 * s1)) : latticeHold;
};

channelRho(t60) = pow(0.001, 1.0 / (t60 * ma.SR)) : latticeHold;

channelBankE(note, mode) = (_ - _'') :
  sum(i, nHarmonics,
      latticeModeE(min(16000, max(16, f(note, i, mode))), rho,
                  ba.lin2LogGain(1 - button("mute_%i"))))
with {
  rho = channelRho((env_mode_change * decay) + 0.05);
};

channelBankR(note, mode) =
  sum(i, nHarmonics,
      latticeModeR(min(16000, max(16, f(note, i, mode))), rho,
                  ba.lin2LogGain(1 - button("mute_%i"))))
with {
  rho = channelRho((env_mode_change * decay) + 0.05);
};
```

The implementation may need explicit ignored driver wires to satisfy Faust 2.59.6, but it
must preserve these equations exactly and the generated checker in Task 5 decides whether
sharing is valid. Do not replace a compiler failure with `fi.tf2np` or another recurrence.

Candidate E's `latticeMode` must apply held `scale` after the custom lattice; Candidate R's
variant must omit `scale`. Both use the historical effective T60 and finish with direct
output multiplication by `1-button("mute_%index")`. The two channel-bank definitions are
the only owners of `channelRho`, so the generated product must contain exactly two decay
`pow` calls.

- [ ] **Step 3: Run transformation tests**

```bash
python3 tests/dsp/make_normalized_lattice_candidates_test.py
```

Expected: PASS.

- [ ] **Step 4: Generate immutable complete candidates with pinned Faust**

```bash
rm -rf /tmp/wingie2-lattice-pair
python3 tests/dsp/make_normalized_lattice_candidates.py \
  /tmp/wingie2-lattice-baseline/Wingie2.dsp /tmp/wingie2-lattice-pair
for candidate in e r; do
  cd "/tmp/wingie2-lattice-pair/$candidate"
  faust2esp32 -ac101 -lib Wingie2.dsp
  unzip -q Wingie2.zip
  shasum -a 256 Wingie2.dsp Wingie2/Wingie2.cpp Wingie2/Wingie2.h \
    > artifact.sha256
  shasum -a 256 --check artifact.sha256
done
cd -
```

Expected: both complete alternate-tuning products generate. On any box or recursive
composition failure, reject that candidate and record the exact compiler output.

- [ ] **Step 5: Prove product files remain historical and commit the generator**

```bash
shasum -a 256 Wingie2.dsp Wingie2/Wingie2.cpp Wingie2/Wingie2.h
git add tests/dsp/make_normalized_lattice_candidates.py \
  tests/dsp/make_normalized_lattice_candidates_test.py
git commit -m "test: 生成 custom normalized-lattice 候选"
```

Expected: product hashes remain historical.

### Task 5: Gate Generated Structure And UI Before Sound Analysis

**Files:**
- Create: `tests/dsp/check_normalized_lattice_generated.py`
- Create: `tests/dsp/check_normalized_lattice_generated_test.py`
- Create: `tests/dsp/list_generated_ui.cpp`

- [ ] **Step 1: Write RED structure-checker fixtures**

Fixtures must independently fail for: missing grouped `mode_changed`, one unguarded lattice
`sqrt`, 19 or 21 lattice square roots, sample-loop division, fewer than 36 paired state
updates, mute ASR state, mute-controlled recurrence, Candidate E missing four delay states,
Candidate R containing a reciprocal, or any source use of `tf2np/nlf2`.

Run the test and expect import failure.

- [ ] **Step 2: Implement generated and UI analysis**

The checker must parse the final `mydsp::compute`, split prologue/sample loop, and emit JSON:

```json
{
  "candidate": "E",
  "block_modulus": 32,
  "decay_pow_calls": 2,
  "mode_cos_calls": 18,
  "c1_sqrt_calls": 18,
  "c2_sqrt_calls": 2,
  "sample_expensive_calls": 0,
  "paired_state_updates": 36,
  "mute_asr_states": 0,
  "mute_recurrence_branches": 0,
  "differentiator_states": 4,
  "output_reciprocals": 18
}
```

Candidate R differs only in `candidate`, `differentiator_states=0`, and
`output_reciprocals=0`. The checker must distinguish the existing note-to-frequency `pow`
calls from the two guarded decay `pow` calls by expression and control-flow location.

`list_generated_ui.cpp` must build `MapUI`, print every path, and return nonzero unless both
`/Wingie/left/mode_changed` and `/Wingie/right/mode_changed` exist and
`/Wingie/mode_changed` does not.

- [ ] **Step 3: Run unit tests and both real candidates**

```bash
python3 tests/dsp/check_normalized_lattice_generated_test.py
for candidate in e r; do
  python3 tests/dsp/check_normalized_lattice_generated.py \
    "/tmp/wingie2-lattice-pair/$candidate/Wingie2.dsp" \
    "/tmp/wingie2-lattice-pair/$candidate/Wingie2/Wingie2.cpp" \
    --candidate "$candidate" \
    --output-json "/tmp/wingie2-lattice-$candidate-structure.json"
done
```

Expected: unit tests and both real structures pass exactly. Reject failures instead of
relaxing counts after observing output.

- [ ] **Step 4: Compile and check UI inventories**

Use `extract_faust_user.py`, compile `list_generated_ui.cpp` twice against the E/R extracted
headers, and save sorted path lists. Diff the lists against the historical extracted class;
only removal of mute-envelope internal behavior is allowed, not path changes.

- [ ] **Step 5: Commit the structure gate**

```bash
git add tests/dsp/check_normalized_lattice_generated.py \
  tests/dsp/check_normalized_lattice_generated_test.py \
  tests/dsp/list_generated_ui.cpp
git commit -m "test: 固定 normalized-lattice 生成结构门禁"
```

### Task 6: Run Isolated And Complete Host Gates

**Files:**
- Create: `tests/dsp/custom_normalized_lattice_contract.dsp`
- Create: `tests/dsp/render_custom_normalized_lattice.cpp`
- Create: `tests/dsp/analyze_custom_normalized_lattice.py`
- Create: `tests/dsp/analyze_custom_normalized_lattice_test.py`
- Create: `tests/dsp/render_generated_wingie_lattice.cpp`
- Create: `tests/dsp/analyze_generated_wingie_lattice.py`
- Create: `tests/dsp/analyze_generated_wingie_lattice_test.py`

- [ ] **Step 1: Write RED analyzer tests**

Reuse the fixed modal-bank peak rule without altering `-18 dB`, `6 dB`, 524,288 samples,
or `+/-5 cents`. Add fixtures for the registered energy inequality, output `8.0`, tail RMS
`1e-9`, E parity `1 cent/2%/0.25 dB`, and mute-state continuation.

- [ ] **Step 2: Create the isolated contract**

Use the exact recurrence and coefficient equations from Task 4. Expose E output, R output,
both `z1/z2` pairs, `s1/c1/s2/c2`, and held-scale probes. The renderer drives boundary,
duplicates, crossing, reverse, unordered ratio-jump, decay-jump, and mute-equivalent
observation sequences for 8,192 samples.

- [ ] **Step 3: Run isolated RED/GREEN**

```bash
faust -a tests/dsp/render_custom_normalized_lattice.cpp -single -ftz 0 \
  tests/dsp/custom_normalized_lattice_contract.dsp \
  -o /tmp/wingie2-lattice-contract.cpp
clang++ -std=c++11 -O2 -I"$FAUST_ROOT/include" \
  /tmp/wingie2-lattice-contract.cpp -o /tmp/wingie2-lattice-contract
python3 tests/dsp/analyze_custom_normalized_lattice_test.py
for scenario in boundary duplicates crossing reverse ratio-jump decay-jump; do
  /tmp/wingie2-lattice-contract "$scenario" "/tmp/lattice-$scenario.f32"
  python3 tests/dsp/analyze_custom_normalized_lattice.py \
    "/tmp/lattice-$scenario.f32" --scenario "$scenario"
done
```

Expected: every state/output is finite, state energy obeys the registered inequality after
excitation, tails remain active, and held coefficients change only at block boundaries.

- [ ] **Step 4: Compile real E/R full-product renderers**

Extract immutable E/R headers and compile `render_generated_wingie_lattice.cpp` against
each. The renderer must require grouped mode-change paths with no shared fallback and cover:

```text
dynamic: decay-step, mode-step, slow-notes, rapid-notes, frequency-jump,
         ratio-jump, mute-state
static:  modes 0/1/2, tunings standard/alternate, indices 0/4/8,
         notes 24/36/60/84/96, Decay 0.1/5/10, both channels
boundary static: indices 0/4/8, 16/50/440/8000/16000 Hz, both channels
peak: 16/440/8000/16000 Hz, indices 0/4/8, both channels
```

Mute-state must excite while all direct outputs are muted, unmute one mode after at least
one second, and prove nonzero current state without adding a probe to the product audio path.

- [ ] **Step 5: Run the complete host matrix**

```bash
python3 tests/dsp/analyze_generated_wingie_lattice_test.py
python3 tests/dsp/analyze_generated_wingie_lattice.py \
  /tmp/wingie2-lattice-baseline-renderer \
  /tmp/wingie2-lattice-e-renderer \
  /tmp/wingie2-lattice-r-renderer \
  /tmp/wingie2-lattice-host.json
```

Expected: all shared finite/bounded/peak/pitch/mute gates pass; E also passes historical
parity; R's gain, T60, DC, and spectral differences are recorded but not judged by parity.

- [ ] **Step 6: Commit host gates**

```bash
git add tests/dsp/custom_normalized_lattice_contract.dsp \
  tests/dsp/render_custom_normalized_lattice.cpp \
  tests/dsp/analyze_custom_normalized_lattice.py \
  tests/dsp/analyze_custom_normalized_lattice_test.py \
  tests/dsp/render_generated_wingie_lattice.cpp \
  tests/dsp/analyze_generated_wingie_lattice.py \
  tests/dsp/analyze_generated_wingie_lattice_test.py
git commit -m "test: 固定 normalized-lattice host 门禁"
```

### Task 7: Measure Generated-Class ESP32 Deadline

**Files:**
- Modify: `tests/esp32/wingie2_generated_benchmark/benchmark.cpp`
- Generate outside repository: `/tmp/wingie2-generated-benchmark-lattice-{e,r}/`
- Write hardware: app0 at `0x10000` only

- [ ] **Step 1: Extend the benchmark stimulus before seeing timing**

Set complete Cave frequency arrays on both sides every block, alternate between the five
registered boundary/duplicate/reverse/unordered sets, change notes `36..95`, and alternate
mute bits without ever skipping recurrence. Keep deterministic nonzero input.

- [ ] **Step 2: Build both immutable generated classes**

```bash
for candidate in e r; do
  tests/esp32/wingie2_generated_benchmark/build_generated_benchmark.sh \
    "/tmp/wingie2-lattice-pair/$candidate/Wingie2/Wingie2.cpp" \
    "lattice-$candidate"
done
```

Expected: Core 2.0.4-cn, valid images, and app0 fit.

- [ ] **Step 3: Measure each candidate with recovery between writes**

For E, then R: validate image, write/verify only `0x10000`, capture one 100,000-block
distribution, parse with `check_deadline.py --expect pass`, restore the historical recovery
image, verify all `0x140000` bytes, read back and `cmp`, then wait for the user's restored-wet
confirmation.

Expected for each survivor: p99 `<=580.5`, max `<725.6`, zero misses. A failure rejects only
that candidate and skips its later tasks.

- [ ] **Step 4: Commit the pre-registered benchmark stimulus**

```bash
git add tests/esp32/wingie2_generated_benchmark/benchmark.cpp
git commit -m "test: 固定 normalized-lattice ESP32 compute 门禁"
```

### Task 8: Add Segmented Complete-Product Timing

**Files:**
- Create: `tests/esp32/wingie2_product_benchmark/instrument_segmented_esp32.py`
- Create: `tests/esp32/wingie2_product_benchmark/instrument_segmented_esp32_test.py`
- Modify: `tests/esp32/wingie2_product_benchmark/build_product_benchmark.sh`

- [ ] **Step 1: Write RED instrumentation tests**

Fixtures must require five independent cycle histograms: input conversion, DSP compute,
output conversion, blocking write, and complete post-read to post-write. Reject missing or
duplicated anchors, timing inside the DSP loop, changed official architecture hashes, or
recording/printing inside the 100,000-block measured hot path.

- [ ] **Step 2: Implement segmented instrumentation**

Use `xthal_get_ccount()` snapshots only at existing phase boundaries. Store histograms in
preallocated memory, print once after capture, and report probe overhead from an empty
snapshot-pair calibration. Keep the original complete interval and its original gate.

- [ ] **Step 3: Build and measure each generated-class survivor**

Use real rapid String pressure for one capture and Cave/future-ratio-equivalent pressure for
a separate capture. Save both distributions and select the slower complete interval as the
candidate's product deadline result.

Expected: the complete interval passes p99/max/miss gates. Segment sums explain scheduling
and blocking behavior but cannot override a failed complete interval.

- [ ] **Step 4: Commit instrumentation**

```bash
python3 tests/esp32/wingie2_product_benchmark/instrument_segmented_esp32_test.py
git add tests/esp32/wingie2_product_benchmark
git commit -m "test: 分段测量 Wingie2 audio deadline"
```

### Task 9: Run Candidate Firmware Pressure And User A/B

**Files:**
- Read: immutable `/tmp/wingie2-lattice-pair/{e,r}/`
- Generate outside repository: `/tmp/wingie2-lattice-firmware-{e,r}/`
- Read/write hardware: app0 `0x10000` only
- Create after measurements: `docs/superpowers/results/2026-07-12-mode-filter-custom-normalized-lattice-results.md`

- [ ] **Step 1: Build normal and diagnostic firmware for machine survivors only**

Copy the historical sketch and libraries to candidate-specific `/tmp` trees, replace only
the generated `Wingie2.cpp/.h` with the immutable candidate pair, and build normal plus
`-DMIDI_DIAGNOSTICS=1` images using Core 2.0.4-cn. Record size, RAM, SHA-256, candidate source
manifest, and image validation.

- [ ] **Step 2: Run E pressure, then restore historical app0**

If E survived, use one persistent serial session and fixed-size counters for left and right
String, Poly, Bar, Cave, and ratio-equivalent batches. After each batch, send low-rate target
and control markers and check target wet, other wet, and dry/thru. Include a long mute,
continued excitation/frequency changes, then unmute.

Stop on parser error, count mismatch, stale parameter, reset, stall, dropout, silence, or
deadline failure. Restore/verify/readback historical app0 and wait for explicit restored-wet
confirmation before any R write.

- [ ] **Step 3: Run R pressure, then restore historical app0**

Repeat the exact E sequence for R without changing batch sizes or gates. Restore and verify
historical app0 afterward.

- [ ] **Step 4: Present surviving candidates for user listening**

For each survivor, ask the user to compare physical pitch, Decay, modal balance, static
response, slow changes, rapid retuning, mute/unmute, both wet sides, and dry/thru. Record the
user's exact decision: `E`, `R`, or `neither`. Do not infer acceptance from machine metrics.

- [ ] **Step 5: Write and commit the evidence result**

The result must include tool/source/generated/image hashes, every host row aggregate,
structure JSON, all deadline distributions and segments, MIDI pressure counts, recovery
proof, exact user confirmations, failed/skipped gates, and one final decision.

```bash
git add docs/superpowers/results/2026-07-12-mode-filter-custom-normalized-lattice-results.md
git commit -m "test: 记录 custom normalized-lattice 结果"
```

### Task 10: Accept One Candidate Or Archive Rejection

**Files:**
- Conditionally modify: `Wingie2.dsp`
- Conditionally regenerate: `Wingie2/Wingie2.cpp`
- Conditionally regenerate: `Wingie2/Wingie2.h`

- [ ] **Step 1: If the user chooses neither, archive and stop**

Verify product hashes remain historical, tag the result, restore/readback historical app0,
and do not merge candidate-specific code into main automatically.

```bash
git diff --exit-code main -- Wingie2.dsp Wingie2/Wingie2.cpp Wingie2/Wingie2.h
git tag -a archive/custom-normalized-lattice-$(date +%Y-%m-%d) \
  -m "Archive custom normalized-lattice result" HEAD
```

- [ ] **Step 2: If the user chooses E or R, install the exact gated artifact**

Copy the immutable candidate `Wingie2.dsp`, generated C++, and header together. Verify all
three hashes against its frozen manifest and prove no other product path changed.

- [ ] **Step 3: Re-run every final gate on the exact tracked artifact**

Run candidate generation/structure/UI tests, complete host matrix, generated-class deadline,
segmented product deadline, normal/diagnostic Core 2.0.4-cn builds, image validation, and
final hardware smoke. Any mismatch rejects installation and restores historical product.

- [ ] **Step 4: Commit the accepted product only after user confirmation**

```bash
git add Wingie2.dsp Wingie2/Wingie2.cpp Wingie2/Wingie2.h
git diff --cached --check
git commit -m "fix: 使用 custom normalized-lattice mode filters"
```

- [ ] **Step 5: Finish on a verified device state**

Explicitly record whether the device ends on the accepted candidate or historical app0.
Verify the selected image at `0x10000`, read back `0x140000` bytes, compare hashes, check
startup MIDI/A3/mode/tuning state, and obtain final user confirmation of both wet paths.

## Final Verification Checklist

```bash
python3 tests/dsp/normalized_lattice_reference_test.py
python3 tests/dsp/make_normalized_lattice_candidates_test.py
python3 tests/dsp/check_normalized_lattice_generated_test.py
python3 tests/dsp/analyze_custom_normalized_lattice_test.py
python3 tests/dsp/analyze_generated_wingie_lattice_test.py
python3 tests/dsp/extract_faust_user_test.py
python3 tests/esp32/check_deadline_test.py
python3 tests/esp32/wingie2_product_benchmark/instrument_segmented_esp32_test.py
swiftc -typecheck Tools/midi_stress.swift
arduino-cli compile --fqbn esp32:esp32:esp32 --libraries Libraries Wingie2
git diff --check
git status --short
```

The test commands, Arduino compile, immutable artifact manifests, ESP32 measurements,
hardware recovery, and user confirmations are separate evidence. None substitutes for
another.
