#pragma once
#include <stdint.h>

// Wi-Fi on demand. Apps call acquire()/release(); the radio is on only while
// someone holds it (plus a short grace period so switching apps doesn't
// reconnect). Connecting never blocks the UI: state changes arrive as
// WifiChanged events.
namespace core::net {

void begin();
void loop();
void acquire();
void release();
void shutdown();  // radio off now (before sleep)
bool connected();
int rssi();
const char *ip();  // "" when not connected

}  // namespace core::net
