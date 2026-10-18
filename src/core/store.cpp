#include "store.h"

#include <Preferences.h>
#include <esp_attr.h>
#include <string.h>

namespace core::store {

static constexpr uint32_t kMagic = 0xA3015EEE;
// Constant-initialised (no constructor at start-up, which would wipe it on every wake).
RTC_DATA_ATTR static RtcState state{};
[[maybe_unused]] static constexpr RtcState kConstantInit{};  // fails to compile if RtcState stops allowing that
static Preferences prefs;
static bool prefsOpen;

void begin() {
  if (state.magic != kMagic) state = RtcState{};
  prefsOpen = prefs.begin("amoled", false);
}

RtcState &rtc() { return state; }
bool rtcValid() { return state.magic == kMagic; }
void rtcCommit() { state.magic = kMagic; }

bool load(const char *key, void *data, size_t len) {
  // isKey() first: getBytesLength() logs an error for a key that was never saved.
  return prefsOpen && prefs.isKey(key) && prefs.getBytesLength(key) == len && prefs.getBytes(key, data, len) == len;
}

bool save(const char *key, const void *data, size_t len) { return prefsOpen && prefs.putBytes(key, data, len) == len; }

static RtcState::Slot *slot(const char *key) {
  for (RtcState::Slot &s : state.slots)
    if (s.key[0] && strncmp(s.key, key, kKeyMax) == 0) return &s;
  return nullptr;
}

bool park(const char *key, const void *data, size_t len) {
  RtcState::Slot *s = slot(key);
  if (!s) {
    if (len > kParkBytes - state.parkedUsed) return false;
    for (RtcState::Slot &free : state.slots) {
      if (free.key[0]) continue;
      strncpy(free.key, key, kKeyMax);
      free.key[kKeyMax] = 0;
      free.offset = state.parkedUsed;
      free.len = uint16_t(len);
      state.parkedUsed += uint16_t(len);
      s = &free;
      break;
    }
  }
  if (!s || s->len != len) return false;
  memcpy(state.parked + s->offset, data, len);
  return true;
}

bool unpark(const char *key, void *data, size_t len) {
  const RtcState::Slot *s = slot(key);
  if (!s || s->len != len) return false;
  memcpy(data, state.parked + s->offset, len);
  return true;
}

}  // namespace core::store
