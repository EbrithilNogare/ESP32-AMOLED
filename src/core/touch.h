#pragma once
#include <stdint.h>
#include <touch_cal.h>

// Capacitive touch: CST820 (V2) or FT3168 (V1). Both report the first touch
// point in the same registers (0x02 count, 0x03..0x06 X/Y), so one small
// driver covers both. The chip is read only after it pulls INT low, so a
// sleeping controller isn't polled.
namespace core::touch {

// `onPress` is asked at the start of every new touch; returning false
// swallows that touch until the finger lifts (used when a touch only wakes
// the screen). `onActivity` is called on every read while a finger is down.
bool begin(uint8_t i2cAddress, bool (*onPress)(), void (*onActivity)());
void sleep();  // controller deep sleep before ESP deep sleep
void kick();   // read on the next poll (after light sleep the INT edge may be missed)

// Raw panel position → screen pixels (lib/TouchCal), applied to every touch.
void setCalibration(const touchcal::Cal &c);
// Raw mode: report the panel's position uncorrected (while calibrating).
void setRaw(bool on);

}  // namespace core::touch
