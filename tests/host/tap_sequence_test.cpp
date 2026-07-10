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
