#pragma once
#include <noise_gen.h>
#include <stdint.h>

// The ES8311 codec and the onboard speaker: short beeps, and continuous
// noise for the Noise app (lib/NoiseGen). Playback runs in its own task; the
// speaker amplifier (PA) is on only while something plays and not muted.
// Volume and mute are user settings (core::settings, quick settings).
namespace core::sound {

enum class Pattern : uint8_t {
  PhaseEnd,  // pomodoro phase finished: three rising tones
  Confirm,   // short single blip
  Alarm,     // countdown finished: four quick beeps (repeated by the Timer app)
};

void begin();
void play(Pattern p);  // dropped while muted
void applyVolume();    // volume or mute changed: takes effect within one buffer (~16 ms)

// Continuous noise, faded in and out (NOISE_FADE_MS). Beeps interrupt it
// briefly. It keeps the chip awake: no light sleep while the output runs.
void startNoise(noise::Kind kind);  // also switches a playing noise to `kind`
void stopNoise();
bool noisePlaying();
bool busy();  // the I2S output is running (noise, a beep, or a fade)

}  // namespace core::sound
