# lib/

Project-private libraries. Each library gets its own folder and PlatformIO
compiles and links it automatically when `src/` includes one of its headers:

```
lib/
└── Display/
    ├── Display.h
    └── Display.cpp
```

Third-party libraries go into `lib_deps` in `platformio.ini` instead.
