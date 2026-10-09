#pragma once
#include <time.h>

// Wall clock: the PCF85063 RTC is the source of truth (it keeps running
// through deep sleep and power-off); SNTP corrects it whenever Wi-Fi is up.
namespace core::clock {

bool begin();        // set TZ, load system time from the RTC
bool valid();        // the time is believable (RTC kept running or SNTP synced)
void onNtpSynced();  // copy system time into the RTC

}  // namespace core::clock
