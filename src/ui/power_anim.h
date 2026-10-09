#pragma once

// The long-press power-off animation and its power-on counterpart: the picture
// collapses to a bright horizontal line, then to a dot, and fades (old CRT
// style). Drawn on the top layer over whatever screen is showing.
namespace ui::power_anim {

void off(void (*done)());  // ~POWER_OFF_ANIM_MS
void on();                 // ~POWER_ON_ANIM_MS, removes itself
bool running();

}  // namespace ui::power_anim
