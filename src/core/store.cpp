#include "store.h"

#include <Preferences.h>
#include <esp_attr.h>

namespace core::store {

static constexpr uint32_t kMagic = 0xA3015EED;
RTC_DATA_ATTR static RtcState state;
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

}  // namespace core::store
