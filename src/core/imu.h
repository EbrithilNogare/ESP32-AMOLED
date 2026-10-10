#pragma once
#include <level_core.h>

// QMI8658 accelerometer (ARCHITECTURE §6). Only the accelerometer is used so
// far: ±2 g, 112 Hz with the on-chip low-pass filter. It stays in standby
// until an app enables it, so it costs nothing while the Level app is closed.
namespace core::imu {

bool begin();  // probe + reset; false if the chip doesn't answer
bool present();
void enable(bool on);
// Gravity in screen axes (see IMU_SCREEN_* in config.h), in g.
bool read(level::Accel &out);

}  // namespace core::imu
