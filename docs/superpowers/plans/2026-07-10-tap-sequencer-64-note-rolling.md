# Tap Sequencer 64-Note Rolling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the unbounded 12-slot Tap Sequencer arrays with a tested state object that keeps and plays the most recent 64 panel notes per channel.

**Architecture:** A header-only `TapSequence` owns note storage, count, and playback cursor, so `control.ino` cannot construct an out-of-range index. The object uses a bounded linear buffer and shifts 63 bytes when full; host tests prove rolling and cursor behavior before firmware integration.

**Tech Stack:** C++11 host assertions, Arduino/ESP32 Core 2.0.4, Arduino CLI, Wingie2 hardware smoke test.

---

## File Structure

- Create: `Wingie2/tap_sequence.h` - fixed-capacity Tap Sequencer state and operations.
- Create: `tests/host/tap_sequence_test.cpp` - deterministic host tests with a reference model.
- Modify: `Wingie2/Wingie2.ino` - include the state type and instantiate one per channel.
- Modify: `Wingie2/control.ino` - route recording and playback through the state API.
- Create after hardware validation: `docs/superpowers/results/2026-07-10-tap-sequencer-64-note-rolling-results.md` - build and hardware evidence.

### Task 1: Establish the Host Regression Test

**Files:**
- Create: `tests/host/tap_sequence_test.cpp`
- Test: `tests/host/tap_sequence_test.cpp`

- [ ] **Step 1: Create the failing state-machine test**

Create `tests/host/tap_sequence_test.cpp`:

```cpp
#include <assert.h>
#include <stdint.h>

#include <deque>

#include "../../Wingie2/tap_sequence.h"

static_assert(TapSequence::kCapacity == 64, "TapSequence capacity changed");

struct ReferenceSequence {
  std::deque<uint8_t> notes;
  uint8_t playIndex = 0;

  void reset(uint8_t note) {
    notes.clear();
    notes.push_back(note);
    playIndex = 0;
  }

  void append(uint8_t note) {
    if (notes.size() < TapSequence::kCapacity) {
      notes.push_back(note);
      return;
    }

    notes.pop_front();
    notes.push_back(note);
    if (playIndex > 0) {
      playIndex--;
    } else {
      playIndex = TapSequence::kCapacity - 1;
    }
  }

  uint8_t advance() {
    playIndex = (playIndex + 1 < notes.size()) ? playIndex + 1 : 0;
    return notes[playIndex];
  }
};

static uint8_t pattern(int index) {
  return uint8_t((index * 7 + index / 5 + 3) % 12);
}

static void testSingleNoteDoesNotCycle() {
  TapSequence sequence;
  uint8_t output = 99;

  assert(sequence.reset(4));
  assert(!sequence.hasCycle());
  assert(!sequence.advance(output));
  assert(output == 99);
}

static void testTwoAndTwelveNoteOrder() {
  TapSequence two;
  uint8_t output = 99;
  assert(two.reset(2));
  assert(two.append(9));
  assert(two.hasCycle());
  assert(two.advance(output) && output == 9);
  assert(two.advance(output) && output == 2);

  TapSequence twelve;
  assert(twelve.reset(0));
  for (uint8_t note = 1; note < 12; note++) {
    assert(twelve.append(note));
  }
  for (uint8_t note = 1; note < 12; note++) {
    assert(twelve.advance(output) && output == note);
  }
  assert(twelve.advance(output) && output == 0);
}

static void testSixtyFiveKeepsNewestWindow() {
  TapSequence sequence;
  ReferenceSequence reference;
  uint8_t output = 99;

  assert(sequence.reset(pattern(0)));
  reference.reset(pattern(0));
  for (int index = 1; index < TapSequence::kCapacity; index++) {
    assert(sequence.append(pattern(index)));
    reference.append(pattern(index));
  }

  // Prove the exact-capacity sequence before exercising rolling.
  for (int index = 0; index < TapSequence::kCapacity; index++) {
    assert(sequence.advance(output));
    assert(output == reference.advance());
  }

  assert(sequence.append(pattern(TapSequence::kCapacity)));
  reference.append(pattern(TapSequence::kCapacity));

  // The 65th note must evict only the original index 0.
  for (int index = 0; index < TapSequence::kCapacity; index++) {
    assert(sequence.advance(output));
    assert(output == reference.advance());
  }
}

static void testRepeatedRollingAndCursorContinuity() {
  TapSequence sequence;
  ReferenceSequence reference;
  uint8_t output = 99;

  sequence.reset(pattern(0));
  reference.reset(pattern(0));
  for (int index = 1; index < 240; index++) {
    assert(sequence.append(pattern(index)));
    reference.append(pattern(index));

    if (index > 1 && index % 9 == 0) {
      assert(sequence.advance(output));
      assert(output == reference.advance());
    }
  }

  for (int index = 0; index < TapSequence::kCapacity * 2; index++) {
    assert(sequence.advance(output));
    assert(output == reference.advance());
    assert(output < 12);
  }
}

static void testInvalidNotesDoNotMutateState() {
  TapSequence empty;
  TapSequence sequence;
  uint8_t output = 99;

  assert(!empty.append(4));
  assert(!empty.reset(12));
  assert(!empty.hasCycle());
  assert(sequence.reset(3));
  assert(sequence.append(8));
  assert(!sequence.append(12));
  assert(!sequence.reset(255));
  assert(sequence.advance(output) && output == 8);
  assert(sequence.advance(output) && output == 3);
}

static void testChannelsAreIndependent() {
  TapSequence left;
  TapSequence right;
  uint8_t output = 99;

  assert(left.reset(1));
  assert(left.append(2));
  assert(right.reset(10));
  assert(right.append(11));

  assert(left.advance(output) && output == 2);
  assert(right.advance(output) && output == 11);
  assert(left.advance(output) && output == 1);
  assert(right.advance(output) && output == 10);
}

int main() {
  testSingleNoteDoesNotCycle();
  testTwoAndTwelveNoteOrder();
  testSixtyFiveKeepsNewestWindow();
  testRepeatedRollingAndCursorContinuity();
  testInvalidNotesDoNotMutateState();
  testChannelsAreIndependent();
  return 0;
}
```

- [ ] **Step 2: Run the test to verify the missing implementation fails**

Run:

```bash
clang++ -std=c++11 -Wall -Wextra -pedantic \
  tests/host/tap_sequence_test.cpp -o /tmp/tap_sequence_test
```

Expected: FAIL with `Wingie2/tap_sequence.h file not found`.

### Task 2: Implement the Bounded TapSequence

**Files:**
- Create: `Wingie2/tap_sequence.h`
- Test: `tests/host/tap_sequence_test.cpp`

- [ ] **Step 1: Add the minimal state implementation**

Create `Wingie2/tap_sequence.h`:

```cpp
#ifndef WINGIE2_TAP_SEQUENCE_H
#define WINGIE2_TAP_SEQUENCE_H

#include <stdint.h>

class TapSequence {
 public:
  static constexpr uint8_t kCapacity = 64;

  TapSequence() : count_(0), playIndex_(0) {}

  bool reset(uint8_t note) {
    if (!validNote(note)) return false;
    notes_[0] = note;
    count_ = 1;
    playIndex_ = 0;
    return true;
  }

  bool append(uint8_t note) {
    if (!validNote(note) || count_ == 0) return false;

    if (count_ < kCapacity) {
      notes_[count_] = note;
      count_++;
      return true;
    }

    for (uint8_t index = 1; index < kCapacity; index++) {
      notes_[index - 1] = notes_[index];
    }
    notes_[kCapacity - 1] = note;

    if (playIndex_ > 0) {
      playIndex_--;
    } else {
      playIndex_ = kCapacity - 1;
    }
    return true;
  }

  bool hasCycle() const {
    return count_ > 1;
  }

  bool advance(uint8_t& note) {
    if (!hasCycle()) return false;
    playIndex_ = (playIndex_ + 1 < count_) ? playIndex_ + 1 : 0;
    note = notes_[playIndex_];
    return true;
  }

 private:
  static bool validNote(uint8_t note) {
    return note < 12;
  }

  uint8_t notes_[kCapacity];
  uint8_t count_;
  uint8_t playIndex_;
};

#endif
```

- [ ] **Step 2: Compile and run the host test**

Run:

```bash
clang++ -std=c++11 -Wall -Wextra -pedantic \
  tests/host/tap_sequence_test.cpp -o /tmp/tap_sequence_test
/tmp/tap_sequence_test
```

Expected: both commands exit 0 with no output.

- [ ] **Step 3: Commit the tested state object**

```bash
git add Wingie2/tap_sequence.h tests/host/tap_sequence_test.cpp
git commit -m "fix: 添加有界 TapSequence 状态"
```

### Task 3: Integrate TapSequence Into Firmware

**Files:**
- Modify: `Wingie2/Wingie2.ino:16-31, 216-219`
- Modify: `Wingie2/control.ino:600-621, 719-742`
- Test: `tests/host/tap_sequence_test.cpp`

- [ ] **Step 1: Include and instantiate TapSequence**

In `Wingie2/Wingie2.ino`, add the local include after `Wingie2.h`:

```cpp
#include "Wingie2.h"
#include "tap_sequence.h"
```

Replace the old Tap Sequencer state:

```cpp
bool trig[2] = {false, false}, trigged[2] = {false, false}, threshChanged[2] = {false, false};
int seq[2][12], seqLen[2] = {0, 0}, playHeadPos[2] = {0, 0}, writeHeadPos[2] = {0, 0};
```

with:

```cpp
bool trig[2] = {false, false}, trigged[2] = {false, false}, threshChanged[2] = {false, false};
TapSequence tapSequence[2];
```

- [ ] **Step 2: Replace direct recording writes**

In the non-Poly/non-Cave key-press branch of `Wingie2/control.ino`, replace first-note initialization with:

```cpp
if (Mode[ch] != POLY_MODE && Mode[ch] != CAVE_MODE) {
  note[ch] = i;
  tapSequence[ch].reset(uint8_t(i));
  if (!ch) dsp.setParamValue("note0", note[ch] + BASE_NOTE + oct[ch] * 12);
  if (ch) dsp.setParamValue("note1", note[ch] + BASE_NOTE + oct[ch] * 12 + 12);
}
```

Replace the subsequent-note branch with:

```cpp
if (Mode[ch] != POLY_MODE && Mode[ch] != CAVE_MODE) {
  note[ch] = i;
  tapSequence[ch].append(uint8_t(i));
  if (!ch) dsp.setParamValue("note0", note[ch] + BASE_NOTE + oct[ch] * 12);
  if (ch) dsp.setParamValue("note1", note[ch] + BASE_NOTE + oct[ch] * 12 + 12);
}
```

- [ ] **Step 3: Replace direct playback indexing**

Replace the Tap Sequencer loop with:

```cpp
trig[0] = dsp.getParamValue("/Wingie/left_trig");
trig[1] = dsp.getParamValue("/Wingie/right_trig");

for (int ch = 0; ch < 2; ch++) {
  if (tapSequence[ch].hasCycle()) {
    if (trig[ch] && !trigged[ch]) {
      trigged[ch] = true;
      uint8_t nextNote;
      if (tapSequence[ch].advance(nextNote)) {
        note[ch] = nextNote;
        if (!ch) dsp.setParamValue("note0", note[ch] + BASE_NOTE + oct[ch] * 12);
        if (ch) dsp.setParamValue("note1", note[ch] + BASE_NOTE + oct[ch] * 12 + 12);
        if (!ch) dsp.setParamValue("/Wingie/left/mode_changed", 1);
        if (ch) dsp.setParamValue("/Wingie/right/mode_changed", 1);
      }
    }
  }
  if (!trig[ch] && trigged[ch]) {
    trigged[ch] = false;
    if (!ch) dsp.setParamValue("/Wingie/left/mode_changed", 0);
    if (ch) dsp.setParamValue("/Wingie/right/mode_changed", 0);
  }
}
```

- [ ] **Step 4: Prove the unsafe state is gone**

Run:

```bash
rg -n '\b(seq|seqLen|writeHeadPos|playHeadPos)\b' Wingie2
```

Expected: no matches.

- [ ] **Step 5: Run host and firmware builds**

Run:

```bash
arduino-cli board details --fqbn esp32:esp32:esp32 | \
  rg 'Board version:.*2\.0\.4'
clang++ -std=c++11 -Wall -Wextra -pedantic \
  tests/host/tap_sequence_test.cpp -o /tmp/tap_sequence_test
/tmp/tap_sequence_test
rm -rf /tmp/wingie2-tap-normal-build /tmp/wingie2-tap-diag-build
arduino-cli compile --fqbn esp32:esp32:esp32 --libraries Libraries \
  --output-dir /tmp/wingie2-tap-normal-build Wingie2
arduino-cli compile --fqbn esp32:esp32:esp32 --libraries Libraries \
  --build-property compiler.cpp.extra_flags=-DMIDI_DIAGNOSTICS=1 \
  --output-dir /tmp/wingie2-tap-diag-build Wingie2
```

Expected: board details resolves to Core 2.0.4 (the local mirror package may display `2.0.4-cn`); host compilation and test exit 0; both Arduino builds exit 0 and report image sizes below 1,310,720 bytes.

- [ ] **Step 6: Commit firmware integration**

```bash
git add Wingie2/Wingie2.ino Wingie2/control.ino
git commit -m "fix: 接入 64 音 Tap Sequencer"
```

### Task 4: Validate on Wingie2 Hardware

**Files:**
- Read: `/dev/cu.usbserial-11310`
- Write hardware: app0 at `0x10000` only
- Create: `docs/superpowers/results/2026-07-10-tap-sequencer-64-note-rolling-results.md`

- [ ] **Step 1: Prepare a reproducible esptool and baseline app0 anchor**

Run:

```bash
python3 -m venv /tmp/wingie2-esptool-venv
/tmp/wingie2-esptool-venv/bin/pip install esptool==3.3
/tmp/wingie2-esptool-venv/bin/esptool.py --chip esp32 \
  --port /dev/cu.usbserial-11310 read_flash \
  0x10000 0x140000 /tmp/wingie2-before-tap-fix-app0.bin
/tmp/wingie2-esptool-venv/bin/esptool.py --chip esp32 \
  image_info /tmp/wingie2-before-tap-fix-app0.bin
shasum -a 256 /tmp/wingie2-before-tap-fix-app0.bin \
  /tmp/wingie2-tap-normal-build/Wingie2.ino.bin
```

Expected: baseline is exactly 1,310,720 bytes and has valid checksum/hash; candidate is a valid ESP32 image smaller than app0.

- [ ] **Step 2: Write and verify candidate app0 only**

Run:

```bash
/tmp/wingie2-esptool-venv/bin/esptool.py --chip esp32 \
  --port /dev/cu.usbserial-11310 --baud 115200 write_flash \
  0x10000 /tmp/wingie2-tap-normal-build/Wingie2.ino.bin
/tmp/wingie2-esptool-venv/bin/esptool.py --chip esp32 \
  --port /dev/cu.usbserial-11310 --baud 115200 verify_flash \
  0x10000 /tmp/wingie2-tap-normal-build/Wingie2.ino.bin
```

Expected: write and verify succeed for offset `0x10000`; no other offset is written.

- [ ] **Step 3: Perform the physical smoke matrix**

On each side separately:

1. Select String mode, record the same sequence of more than 12 notes that previously produced the extreme pulse, and trigger at least two full cycles.
2. Confirm every output stays in the expected pitch set, order repeats, and the other side is unchanged.
3. Repeat in Bar mode.
4. Confirm dry/thru and both wet paths remain audible.

Expected: no extreme short pulse, no out-of-range pitch, no cross-channel sequence corruption, and no reset.

- [ ] **Step 4: Restore the baseline app0 after validation**

Run:

```bash
/tmp/wingie2-esptool-venv/bin/esptool.py --chip esp32 \
  --port /dev/cu.usbserial-11310 --baud 115200 write_flash \
  0x10000 /tmp/wingie2-before-tap-fix-app0.bin
/tmp/wingie2-esptool-venv/bin/esptool.py --chip esp32 \
  --port /dev/cu.usbserial-11310 --baud 115200 verify_flash \
  0x10000 /tmp/wingie2-before-tap-fix-app0.bin
```

Expected: verify digest matches; startup reports MIDI channels 1/2/3, A3 440, Poly modes, and standard tuning; both wet paths are audible.

- [ ] **Step 5: Record and commit results**

Create `docs/superpowers/results/2026-07-10-tap-sequencer-64-note-rolling-results.md` with exact host command, both Arduino size reports, baseline/candidate hashes, app0 verify results, and separate left/right String/Bar observations. Do not mark a physical check PASS unless the user explicitly confirmed it.

Then run:

```bash
git add docs/superpowers/results/2026-07-10-tap-sequencer-64-note-rolling-results.md
git commit -m "test: 记录 Tap Sequencer 修复结果"
git status --short
```

Expected: result commit succeeds and the worktree is clean.
