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
