#pragma once

// Drawn on the top layer over whatever screen is showing (UI_DESIGN §7):
// - off: screen off (power button). The picture collapses to a bright
//   horizontal line, then to a dot, and fades (old CRT style).
// - on: its reverse, when the screen comes back.
// - shutdown: power off (button held), longer and different on purpose: the
//   picture fades out, a ring winds round the power symbol, then unwinds and
//   takes the symbol with it, down to a dot.
// off and shutdown leave the screen covered in black until on().
namespace ui::power_anim {

void off(void (*done)());       // ~POWER_OFF_ANIM_MS
void shutdown(void (*done)());  // ~SHUTDOWN_ANIM_MS
void on();                      // ~POWER_ON_ANIM_MS, removes itself
bool running();

}  // namespace ui::power_anim
