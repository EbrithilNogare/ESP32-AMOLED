#include "multi_tap.h"

#include <string.h>

namespace multitap {

namespace {

// Letters and punctuation as on a Nokia keypad, ending in the key's digit.
// '1' and '*' carry what Wi-Fi passwords tend to need.
const char *const kLower[10] = {" 0", ".,-_@!?1", "abc2", "def3", "ghi4", "jkl5", "mno6", "pqrs7", "tuv8", "wxyz9"};
const char *const kUpper[10] = {" 0", ".,-_@!?1", "ABC2", "DEF3", "GHI4", "JKL5", "MNO6", "PQRS7", "TUV8", "WXYZ9"};
const char *const kDigits[10] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
const char kSymbols[] = "#$%&*+=/\\:;'\"()[]{}<>^~`|";

}  // namespace

const char *charsFor(char key, Mode mode) {
  if (key == '*') return kSymbols;
  if (key < '0' || key > '9') return "";
  const int k = key - '0';
  return mode == Mode::Digits ? kDigits[k] : mode == Mode::Upper ? kUpper[k] : kLower[k];
}

const char *modeName(Mode mode) { return mode == Mode::Digits ? "123" : mode == Mode::Upper ? "ABC" : "abc"; }

bool MultiTap::press(char key, uint32_t nowMs) {
  if (key == '#') {
    pendingKey_ = 0;
    mode_ = mode_ == Mode::Lower ? Mode::Upper : mode_ == Mode::Upper ? Mode::Digits : Mode::Lower;
    return true;
  }
  const char *chars = charsFor(key, mode_);
  const size_t n = strlen(chars);
  if (!n) return false;

  // Same key again within the pause: the pending character moves on.
  if (key == pendingKey_ && nowMs - lastMs_ < commitMs_) {
    pendingIdx_ = uint8_t((pendingIdx_ + 1) % n);
    buf_[len_ - 1] = chars[pendingIdx_];
    lastMs_ = nowMs;
    return true;
  }
  pendingKey_ = 0;  // anything pending is committed
  if (len_ == kMaxLen) return false;
  buf_[len_++] = chars[0];
  buf_[len_] = '\0';
  if (n > 1) {  // single-character keys (digits mode) commit at once
    pendingKey_ = key;
    pendingIdx_ = 0;
  }
  lastMs_ = nowMs;
  return true;
}

bool MultiTap::backspace() {
  pendingKey_ = 0;
  if (!len_) return false;
  buf_[--len_] = '\0';
  return true;
}

bool MultiTap::update(uint32_t nowMs) {
  if (!pendingKey_ || nowMs - lastMs_ < commitMs_) return false;
  pendingKey_ = 0;
  return true;
}

void MultiTap::clear() {
  len_ = 0;
  buf_[0] = '\0';
  pendingKey_ = 0;
}

}  // namespace multitap
