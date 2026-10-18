# Noise

## Purpose

A sound machine: rain with a matching rain animation, white noise and blue
noise. Meant to run for a long time (focus, sleep), so it is careful with the
battery and the AMOLED.

## Screens

```
 Paused                        Playing (first 4 s)           Playing, untouched
┌──────────────────────────┐  ┌──────────────────────────┐  ┌──────────────────────────┐
│ ╱    Rain    ╱      ╱    │  │ ╱    Rain    ╱      ╱    │  │ ╱      ╱     ╱      ╱    │
│  tap to play · swipe ╱   │  │ ╱   playing    ╱     ╱   │  │   ╱      ╱      ╱     ╱  │
│   ╱      ╱      ╱        │  │   ╱      ╱      ╱        │  │   ╱      ╱      ╱        │
│ ╱     (  ▶  )     ╱      │  │ ╱      ╱     ╱     ╱     │  │ ╱      ╱     ╱     ╱     │
│   ╱      ╱      ╱    ╱   │  │   ╱   .:.    ╱    ╱      │  │   ╱      ╱  .:.  ╱       │  splashes
│ ╱     ╱      ╱      ╱    │  │ ╱     ╱      ╱      ╱    │  │ ╱     ╱      ╱      ╱    │
│          ● ○ ○           │  │          ● ○ ○           │  │                          │
└──────────────────────────┘  └──────────────────────────┘  └──────────────────────────┘
  still and dimmed              the labels fade out after     only the animation
                                4 s without a touch
```

| Sound | Picture | How it's made |
| --- | --- | --- |
| Rain | streaks in three depths (far ones short, dim, slow), wind, splashes | pink noise band-passed 150 Hz–3 kHz, swelling in slow gusts, plus ~28 drops/s: short noise ticks, some with a rising "plink" tone |
| White noise | soft snow drifting down | uniform random samples |
| Blue noise | a jittered grid of twinkling blue points rising slowly | the first difference of pink noise (+3 dB/octave) |

All three are generated live (`lib/NoiseGen`, host-tested: loudness, spectrum
tilt, no clipping) at 16 kHz mono and come out at about the same loudness.
Nothing is stored in flash.

Simulator frames: `docs/design/screens/38-noise-paused.png` …
`42-noise-blue.png`.

## Interaction

| Input | Effect |
| --- | --- |
| Tap | play ↔ pause (fades in / out over 0.4 s) |
| Swipe left / right | next / previous sound (Rain → White → Blue → Rain), and play it |
| Any touch | the labels come back for 4 s |
| Back / Home | to the menu; **the sound keeps playing**. Open the app again to pause it |
| Power held (power off) | stops the sound |

Volume and mute are in the [quick settings](quick-settings.md). While muted,
the app says so ("Muted: unmute in quick settings") and the speaker
amplifier stays off.

## Power and burn-in

- Policy `Normal`: the screen dims and goes off on the normal timeline (or
  with Power). While it plays (open or in the background) Noise asks to
  keep the chip awake with the screen off (`sleepNeed().awake`): the device
  dozes, CPU at 80 MHz, and the sound goes on. With the screen off nothing
  is drawn. Paused, nothing is needed and the device deep-sleeps.
- A power-off stops the sound; afterwards Noise opens paused.
- No light sleep while sound plays (it would stop the I2S stream). If a
  minimal view is up meanwhile (a running Pomodoro or Timer), it stays, just
  without light sleep.
- The animation runs at 24 fps, 8 fps when dimmed. Every particle keeps
  moving, the labels hide after 4 s, and the screen goes off after the idle
  time (40 s on battery, 10 min on the charger): nothing static stays lit.
- Battery: the speaker, amplifier and an awake CPU drain it in hours, not
  days. Measure and record in HARDWARE.md.

## Persistence

NVS key `noise`: the last sound. Volume and mute: see quick settings.

## Config

```cpp
// ---- noise (docs/apps/noise.md) ----
#define NOISE_COLOR 0x6E8BFF
#define NOISE_FPS 24
#define NOISE_FPS_DIM 8
#define NOISE_FADE_MS 400
```

## Errors

If the codec doesn't answer, nothing plays and the app falls back to
"tap to play" (the error is logged).

## Acceptance (on device)

- [ ] Rain sounds like rain, not like hiss with clicks; white and blue are steady
- [ ] Swiping changes the sound without a click or a jump in loudness
- [ ] Plays on with the screen black, in the menu and in other apps
- [ ] No hiss from the speaker while paused or muted
- [ ] Beeps (Pomodoro, Timer) still come through while it plays
- [ ] Current while playing with the screen black measured and recorded

## Later

A sleep timer (fade out after 30 / 60 min), brown and pink noise, ocean and
fireplace, volume on the side buttons.
