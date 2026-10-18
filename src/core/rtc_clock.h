#pragma once
#include <stdint.h>
#include <time.h>

// Wall clock: the PCF85063 RTC is the source of truth (it keeps running
// through deep sleep and power-off); SNTP corrects it whenever Wi-Fi is up.
// Also carries core::mono() (ticks.h) through deep sleep.
namespace core::clock {

// Set TZ and the system time from the RTC. After a deep sleep the ESP's own
// clock kept running with sub-second precision, so it's kept unless it
// drifted from the RTC by more than a second (its RC oscillator over a long
// sleep).
bool begin(bool afterDeepSleep);
bool valid();        // the time is believable (RTC kept running or SNTP synced)
void onNtpSynced();  // copy system time into the RTC
int64_t wallMs();    // system time in ms (counts on through deep sleep even when not valid)

void noteSleep(uint64_t monoNow);  // going to deep sleep: remember mono() and the wall clock
void resume();                     // after a deep-sleep wake: mono() carries on from there

}  // namespace core::clock
