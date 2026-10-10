// Nokia-style multi-tap text entry (docs/apps/wifi.md): tap a key repeatedly
// to cycle through its characters; the character commits after a pause or
// when another key is pressed. '#' cycles abc → ABC → 123, '*' cycles
// through symbols. Used for typing Wi-Fi passwords on the keypad.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace multitap {

enum class Mode : uint8_t { Lower, Upper, Digits };

constexpr size_t kMaxLen = 63;  // a WPA2 passphrase

// Characters a key cycles through in a mode ('0'..'9', '*'); "" for others.
const char *charsFor(char key, Mode mode);
const char *modeName(Mode mode);  // "abc", "ABC", "123"

class MultiTap {
 public:
  explicit MultiTap(uint32_t commitMs = 900) : commitMs_(commitMs) {}

  // A keypad key: '0'..'9' and '*' type, '#' switches the mode. Returns true
  // if the text or the mode changed.
  bool press(char key, uint32_t nowMs);
  bool backspace();             // removes the last character (pending or not)
  bool update(uint32_t nowMs);  // commits a pending character after the pause
  void clear();

  const char *text() const { return buf_; }  // includes the pending character
  size_t length() const { return len_; }
  bool pending() const { return pendingKey_ != 0; }  // the last character still cycles
  Mode mode() const { return mode_; }

 private:
  uint32_t commitMs_;
  char buf_[kMaxLen + 1] = {};
  size_t len_ = 0;
  Mode mode_ = Mode::Lower;
  char pendingKey_ = 0;  // key whose character is still cycling, 0 if none
  uint8_t pendingIdx_ = 0;
  uint32_t lastMs_ = 0;
};

}  // namespace multitap
