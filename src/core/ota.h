#pragma once
#include <stdint.h>

// Wireless firmware updates (ARCHITECTURE §12): push from the PC with
// ArduinoOTA/espota, listening only while System → Wireless update is open.
// A freshly updated image is confirmed only after a health check; if it
// crashes before that, the bootloader rolls back to the previous slot.
namespace core::ota {

enum class State : uint8_t { Off, WaitingForWifi, Waiting, Receiving, Done, Error };

void begin();       // rollback bookkeeping at boot
void healthTick();  // call every loop; confirms a pending image once healthy
bool start();       // false (and error() says why) if OTA can't run now
void stop();
void loop();
// ArduinoOTA receives the whole image inside one loop() call; this hook is
// called from there (at most every 100 ms) so the progress UI keeps drawing.
void setProgressHook(void (*hook)());
State state();
uint8_t progress();  // 0-100 while receiving
const char *error();

}  // namespace core::ota
