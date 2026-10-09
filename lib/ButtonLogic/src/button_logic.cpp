#include "button_logic.h"

namespace btn {

Button::Button(const Config &c) : cfg_(c) {}

Event Button::update(bool rawPressed, uint32_t nowMs) {
  if (!started_ || rawPressed != raw_) {
    raw_ = rawPressed;
    rawSinceMs_ = nowMs;
    started_ = true;
  }
  // Unsigned subtraction keeps working across the 49-day millis() wrap.
  const bool stable = nowMs - rawSinceMs_ >= cfg_.debounceMs;

  if (stable && raw_ != held_) {
    held_ = raw_;
    if (held_) {
      pressedAtMs_ = nowMs;
      longSent_ = false;
      return Event::Pressed;
    }
    const bool quiet = longSent_ || suppressed_;
    suppressed_ = false;
    return quiet ? Event::None : Event::Short;
  }

  if (held_ && !longSent_ && !suppressed_ && nowMs - pressedAtMs_ >= cfg_.longMs) {
    longSent_ = true;
    return Event::Long;
  }

  // A suppressed press that never got through the debounce must not leak
  // into the next real press.
  if (stable && !held_) suppressed_ = false;
  return Event::None;
}

bool Button::held() const { return held_; }

void Button::suppress() {
  if (held_ || raw_) suppressed_ = true;
}

}  // namespace btn
