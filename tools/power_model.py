#!/usr/bin/env python3
"""Estimate each app's battery current: screen on, screen off, and the
device-wide sleep states. Prints the tables in docs/POWER.md.

    python tools/power_model.py

Every figure is a datasheet or rule-of-thumb ESTIMATE, not a measurement:
nobody has put a meter on the board yet (docs/HARDWARE.md, "Measuring
it"). Once measured, change the numbers in PARTS / APPS and rerun; the
tables follow. Currents are from the battery (the AXP2101's converters are
folded in at ~90 %).
"""

BATTERY_MAH = 400  # the recommended cell (docs/HARDWARE.md)

# ---- parts (mA) ------------------------------------------------------------
PARTS = {
    # ESP32-S3 at 240 MHz, mostly waiting in delay() between loop passes
    # (WAITI), with the OPI PSRAM idle and the I2C polls (PMU every 50 ms).
    "cpu_idle": 33.0,
    # Extra while LVGL renders and flushes over QSPI, at 100 % duty.
    "cpu_render": 35.0,
    # ESP32-S3 at 80 MHz, dozing with the screen off (noise feeding I2S).
    "cpu_doze_80": 20.0,
    # AMOLED: driver and scan with the panel on (black), plus emission at
    # full white and full brightness, scaled by the picture level and the
    # brightness (0x51 taken as linear).
    "panel_base": 8.0,
    "panel_white": 110.0,
    "touch": 2.5,          # CST820 scanning (asleep: ~0)
    "imu": 0.3,            # QMI8658 accelerometer only, ~125 Hz
    "audio": 25.0,         # ES8311 + speaker amp, noise at a middle volume
    "wifi": 30.0,          # associated, modem sleep, with HA traffic
    "misc": 1.0,           # AXP2101, TCA9554, RTC, pull-ups, LDO losses
    # Screen-off states (docs/HARDWARE.md, Deep-sleep current).
    "deep_sleep": 0.15,    # 0.1-0.2 mA estimated: every part asleep
    "light_sleep": 1.5,    # doze without sound: light sleep, a wake every <= 5 s
    "timer_wake_mas": 16.0,  # one silent deadline wake: ~0.4 s at ~40 mA (mA*s)
}

BRIGHT_ACTIVE = 0.60   # BRIGHTNESS_USER_DEFAULT_PCT
BRIGHT_MINIMAL = 38 / 255  # BRIGHTNESS_MINIMAL

# ---- apps ------------------------------------------------------------------
# apl:    average picture level, 0..1 of a full-white screen (estimated from
#         the simulator frames: how much of the screen is lit, how brightly)
# render: share of the time spent drawing while open (animations)
# extra:  parts on while open
# off:    what it needs with the screen off (its sleepNeed())
#         ("deep", "deep+timer", "doze+audio"); deep+timer:N wakes every N s
APPS = [
    # name, apl, render, extra, off, note
    ("Menu",            0.06, 0.01, [],        "deep",       ""),
    ("Lights (HA)",     0.10, 0.02, ["wifi"],  "deep",       "Wi-Fi up while open"),
    ("Bot face",        0.08, 0.15, [],        "deep",       "30 fps face"),
    ("Hephy",           0.06, 0.15, [],        "deep",       "30 fps face + sparks; lives on the wall clock"),
    ("Pomodoro",        0.05, 0.01, [],        "deep+timer:1500", "running: a silent wake at every phase end (25 min)"),
    ("Espresso",        0.05, 0.05, [],        "deep",       "a running shot stops on sleep"),
    ("Dice",            0.04, 0.05, [],        "deep",       "rolling: ~60 % render for 1.2 s"),
    ("Magic 8",         0.03, 0.02, ["imu"],   "deep",       "text only; IMU at 50 Hz reads while open"),
    ("Level",           0.06, 0.20, ["imu"],   "deep",       "IMU off with the screen"),
    ("Timer: clock",    0.10, 0.01, [],        "deep",       "LowPowerDisplay on the clock tab"),
    ("Timer: countdown", 0.08, 0.01, [],       "deep+timer:86400", "one silent wake at the end, then the alarm"),
    ("Noise (playing)", 0.04, 0.05, ["audio"], "doze+audio", "keeps playing with the screen off"),
    ("2048",            0.18, 0.03, [],        "deep",       "the brightest app: warm tiles"),
    ("System",          0.05, 0.01, [],        "deep",       ""),
]


def panel(apl: float, bright: float) -> float:
    return PARTS["panel_base"] + PARTS["panel_white"] * apl * bright


def on_current(apl: float, render: float, extra: list[str]) -> float:
    i = PARTS["cpu_idle"] + PARTS["cpu_render"] * render + PARTS["touch"] + PARTS["misc"]
    i += panel(apl, BRIGHT_ACTIVE)
    for e in extra:
        i += PARTS[e]
    return i


def off_current(kind: str) -> float:
    if kind == "deep":
        return PARTS["deep_sleep"]
    if kind.startswith("deep+timer:"):  # a silent boot per deadline, amortised
        return PARTS["deep_sleep"] + PARTS["timer_wake_mas"] / float(kind.split(":")[1])
    if kind == "doze+audio":
        return PARTS["cpu_doze_80"] + PARTS["audio"] + PARTS["misc"]
    if kind == "minimal":  # light sleep + the panel at minimal brightness, sparse view
        return PARTS["light_sleep"] + panel(0.02, BRIGHT_MINIMAL) + PARTS["misc"]
    raise ValueError(kind)


def life(ma: float) -> str:
    h = BATTERY_MAH / ma
    if h < 48:
        return f"{h:.0f} h" if h >= 10 else f"{h:.1f} h"
    d = h / 24
    return f"{d:.0f} days" if d < 60 else f"{d / 30:.0f} months"


OFF_NAMES = {
    "deep": "deep sleep",
    "deep+timer": "deep sleep + timer wake",
    "doze+audio": "doze, sound on",
    "minimal": "minimal view",
}


def main() -> None:
    print(f"Battery: {BATTERY_MAH} mAh. Screen on at {BRIGHT_ACTIVE:.0%} brightness.\n")
    print("| App | Open, screen on | Battery life open | Screen off (its need) | Battery life off | Note |")
    print("| --- | ---: | ---: | --- | ---: | --- |")
    for name, apl, render, extra, off, note in APPS:
        on = on_current(apl, render, extra)
        o = off_current(off)
        print(f"| {name} | {on:.0f} mA | {life(on)} | {OFF_NAMES[off.split(':')[0]]}: {o:.2f} mA | {life(o)} | {note} |")

    awake_dark = PARTS["cpu_idle"] + PARTS["misc"]  # no sleep plan: chip up, screen and touch off
    deep = PARTS["deep_sleep"]
    print("\n| Device state | Current | Battery life |")
    print("| --- | ---: | ---: |")
    rows = [
        ("Screen off, no sleep at all (chip awake at 240 MHz, panel and touch asleep)", awake_dark),
        ("Screen off, light-sleep doze only (what charger mode does)", PARTS["light_sleep"]),
        ("Screen off, no app needs anything: deep sleep", deep),
        ("Power-off (Power held): deep sleep, button wake only", deep),
        ("Minimal view: Pomodoro or Timer idle with the screen on (dim, light sleep between redraws)",
         off_current("minimal")),
        ("Noise playing with the screen off", off_current("doze+audio")),
    ]
    for label, ma in rows:
        print(f"| {label} | {ma:.2f} mA | {life(ma)} |")

    # A day of use: the screen on for N minutes in short visits (a typical
    # app at ~52 mA, plus the dim stage before each sleep), the rest asleep.
    print("\n| A day of use | Average | Battery life |")
    print("| --- | ---: | ---: |")
    typical = 52.0
    dimmed = PARTS["cpu_idle"] + PARTS["touch"] + PARTS["misc"] + panel(0.06, 64 / 255)
    for minutes, visits in ((10, 10), (30, 20), (60, 30), (120, 40)):
        dim_min = visits * 10 / 60  # each visit ends with ~10 s dimmed (most people press Power sooner)
        asleep = 1440 - minutes - dim_min
        avg = (minutes * typical + dim_min * dimmed + asleep * deep) / 1440
        print(f"| {minutes} min on in {visits} visits | {avg:.2f} mA | {life(avg)} |")


if __name__ == "__main__":
    main()
