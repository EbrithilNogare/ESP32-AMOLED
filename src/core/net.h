#pragma once
#include <stdint.h>

// Wi-Fi on demand. Apps call acquire()/release(); the radio is on only while
// someone holds it (plus a short grace period so switching apps doesn't
// reconnect). Connecting never blocks the UI: link up/down transitions arrive
// as WifiChanged events. With an empty WIFI_SSID the radio never starts.
namespace core::net {

void begin();
void loop();
void acquire();
void release();
void shutdown();  // radio off now (before deep sleep)
// While suspended (minimal view: light sleep and Wi-Fi don't mix) the radio
// stays off even if apps hold it; it comes back when the suspension ends.
void suspend(bool on);
bool connected();
int rssi();
const char *ip();  // "" when not connected

}  // namespace core::net
