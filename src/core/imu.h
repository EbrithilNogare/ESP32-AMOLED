#pragma once
#include <level_core.h>

// QMI8658 accelerometer (ARCHITECTURE §6). Only the accelerometer is used so
// far: ±2 g, ≈ 112–125 Hz (the datasheet ODR depends on the mode) with the
// on-chip low-pass filter. It's powered down (oscillator off too) until an app
// enables it: only the Level app does, while it's open and the screen is on.
namespace core::imu {

// Probe (+ reset on a cold boot), then power down; false if the chip doesn't answer.
bool begin(bool coldBoot);
bool present();
void enable(bool on);  // configure and run / power down again
// Gravity in screen axes (see IMU_SCREEN_* in config.h), in g.
bool read(level::Accel &out);

}  // namespace core::imu
