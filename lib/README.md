# lib/

Project-private libraries: Arduino-free logic that the firmware and the PC
host tests share. PlatformIO compiles and links a library automatically when
`src/` includes one of its headers.

Every library has the same layout, and its Unity tests live in
`test/test_<snake_case>/` (run with `pio test -e native`):

```
lib/
└── DiceCore/
    ├── library.json          name, version, description
    └── src/
        ├── dice_core.h
        └── dice_core.cpp
test/
└── test_dice_core/
    └── test_main.cpp
```

Current libraries: `BotFace`, `ButtonLogic`, `DiceCore`, `HaParse`,
`LevelCore`, `MultiTap`, `NoiseGen`, `PixelShift`, `PomodoroCore`, `PowerFsm`,
`ShotTimer`, `SleepLog`, `TimerCore`, `TouchCal`, `WireRaster`.
Adapters in `src/` connect them to hardware and LVGL.

Third-party libraries go into `lib_deps` in `platformio.ini` instead.
