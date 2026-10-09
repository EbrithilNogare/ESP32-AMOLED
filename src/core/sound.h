#pragma once
#include <stdint.h>

// Short beeps through the ES8311 codec and the onboard speaker. Playback runs
// in its own task; the speaker amplifier (PA) is on only while a beep plays.
namespace core::sound {

enum class Pattern : uint8_t {
  PhaseEnd,  // pomodoro phase finished: three rising tones
  Confirm,   // short single blip
};

void begin();
void play(Pattern p);

}  // namespace core::sound
