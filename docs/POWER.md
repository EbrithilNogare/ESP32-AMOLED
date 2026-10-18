# Power estimates

What every app costs from the battery: open with the screen on, and with the
screen off (what it asks for with `sleepNeed()`, ARCHITECTURE §8). Also
covered: the device-wide states (no sleep at all, deep sleep, power-off) and
a few typical days.

**These are estimates, not measurements.** Nobody has put a meter on the
board yet ([HARDWARE.md](HARDWARE.md), "Measuring it"). The figures come
from datasheet typicals and rules of thumb, listed below. The tables are
printed by `python tools/power_model.py`: after measuring, change the
numbers there and paste its output here again.

## Assumptions

| Part | Assumed | Basis |
| --- | --- | --- |
| ESP32-S3, 240 MHz, loop waiting in `delay()` (+ PSRAM idle, PMU polls) | 33 mA | datasheet modem-sleep figures, CPU mostly in WAITI |
| … rendering (LVGL draw + QSPI flush), at 100 % duty | +35 mA | the second core idle, flash/PSRAM busy |
| ESP32-S3 at 80 MHz, dozing with sound | 20 mA | `DOZE_CPU_MHZ` |
| AMOLED, on, black | 8 mA | driver + scan; OLED pixels that are off cost nothing |
| AMOLED emission, full white at full brightness | 110 mA | 1.8" AMOLED; scaled by picture level and brightness (0x51 taken as linear) |
| CST820 touch, scanning | 2.5 mA | asleep: ~0 |
| QMI8658, accelerometer only | 0.3 mA | gyro never on |
| ES8311 + speaker amp, noise at a middle volume | 25 mA | louder costs more |
| Wi-Fi associated, modem sleep, HA traffic | 30 mA | bursts reach 300 mA |
| AXP2101, TCA9554, RTC, pull-ups, LDO losses | 1 mA | |
| Deep sleep, everything asleep | 0.15 mA | 0.1–0.2 mA, [HARDWARE.md](HARDWARE.md) |
| Light-sleep doze (a wake every ≤ 5 s) | 1.5 mA | |
| One silent deadline wake | ~0.4 s at ~40 mA | the panel stays dark |

- **Brightness:** the default user brightness, 60 %.
- **Picture level:** how much of a full-white screen each app lights,
  estimated from the simulator frames.
  - Most apps light 4–10 % of a white screen. 2048's warm tiles reach about
    18 %, so it's the brightest.
  - At 60 % brightness, the panel costs 10–20 mA. The CPU (33 mA+) costs more
    than the screen in every app.
- **Battery:** the recommended 400 mAh cell.

## Per app

Battery: 400 mAh. Screen on at 60% brightness.

| App | Open, screen on | Battery life open | Screen off (its need) | Battery life off | Note |
| --- | ---: | ---: | --- | ---: | --- |
| Menu | 49 mA | 8.2 h | deep sleep: 0.15 mA | 4 months |  |
| Lights (HA) | 82 mA | 4.9 h | deep sleep: 0.15 mA | 4 months | Wi-Fi up while open |
| Bot face | 55 mA | 7.3 h | deep sleep: 0.15 mA | 4 months | 30 fps face |
| Hephy | 54 mA | 7.4 h | deep sleep: 0.15 mA | 4 months | 30 fps face + sparks; lives on the wall clock |
| Pomodoro | 48 mA | 8.3 h | deep sleep + timer wake: 0.16 mA | 3 months | running: a silent wake at every phase end (25 min) |
| Espresso | 50 mA | 8.1 h | deep sleep: 0.15 mA | 4 months | a running shot stops on sleep |
| Dice | 49 mA | 8.2 h | deep sleep: 0.15 mA | 4 months | rolling: ~60 % render for 1.2 s |
| Magic 8 | 51 mA | 7.9 h | deep sleep: 0.15 mA | 4 months | IMU at 50 Hz reads while open |
| Level | 56 mA | 7.2 h | deep sleep: 0.15 mA | 4 months | IMU off with the screen |
| Timer: clock | 51 mA | 7.8 h | deep sleep: 0.15 mA | 4 months | LowPowerDisplay on the clock tab |
| Timer: countdown | 50 mA | 8.0 h | deep sleep + timer wake: 0.15 mA | 4 months | one silent wake at the end, then the alarm |
| Noise (playing) | 74 mA | 5.4 h | doze, sound on: 46.00 mA | 8.7 h | keeps playing with the screen off |
| 2048 | 57 mA | 7.0 h | deep sleep: 0.15 mA | 4 months | the brightest app: warm tiles |
| System | 48 mA | 8.3 h | deep sleep: 0.15 mA | 4 months |  |

## Device states

| Device state | Current | Battery life |
| --- | ---: | ---: |
| Screen off, no sleep at all (chip awake at 240 MHz, panel and touch asleep) | 34.00 mA | 12 h |
| Screen off, light-sleep doze only (what charger mode does) | 1.50 mA | 11 days |
| Screen off, no app needs anything: deep sleep | 0.15 mA | 4 months |
| Power-off (Power held): deep sleep, button wake only | 0.15 mA | 4 months |
| Minimal view: Pomodoro or Timer idle with the screen on (dim, light sleep between redraws) | 10.83 mA | 37 h |
| Noise playing with the screen off | 46.00 mA | 8.7 h |

## Typical days

| A day of use | Average | Battery life |
| --- | ---: | ---: |
| 10 min on in 10 visits | 0.56 mA | 30 days |
| 30 min on in 20 visits | 1.34 mA | 12 days |
| 60 min on in 30 visits | 2.47 mA | 7 days |
| 120 min on in 40 visits | 4.68 mA | 4 days |

## What it means

- **Open, any app costs 50–60 mA**, so 7–8 hours of screen time. The CPU at
  240 MHz is most of it, not the panel. Home Assistant (Wi-Fi) and Noise
  (the speaker) cost the most.
- **Screen off, almost every app costs nothing extra.**
  - Most apps need nothing with the screen off, so the device deep-sleeps
    at ~0.15 mA, about 4 months on the cell.
  - A countdown or a running Pomodoro adds one silent wake per deadline:
    ~16 mA·s each, which is noise next to the deep sleep.
  - The exception is Noise: playing on with the screen off costs ~46 mA,
    which is hours.
- **Without the sleep plan** (chip awake, screen off), the same "off" would
  cost ~34 mA: half a day instead of months. The doze that charger mode uses
  costs ~1.5 mA, about 11 days.
- **Power-off is the same deep sleep, minus the timer wakes.** The AXP2101
  shutdown would cost a few µA, but it can only be ended with PWR, and BOOT
  is the power button ([DECISIONS.md](DECISIONS.md), 2026-10-18).
- **Hephy's sleep costs nothing.** He lives on the wall clock (PCF85063):
  everything is worked out when the app opens, so he never wakes the device.
- **Screen time decides how long a charge lasts.** At 30 minutes of screen time a day, the
  cell lasts about 12 days; the sleeping hours in between cost little.

## Measuring it

Follow [HARDWARE.md](HARDWARE.md), "Measuring it":

1. Measure deep sleep first: everything here rests on that 0.15 mA.
2. Measure one screen-on app at 60 % brightness: the CPU and panel
   figures.
3. Put both into `PARTS` in `tools/power_model.py`, rerun it, and replace
   the tables.

What would cut the screen-on cost the most, if measuring shows it's
needed:
- Run the CPU at 160 or 80 MHz between animations (dynamic frequency
  scaling).
- Light-sleep between frames on static screens.
