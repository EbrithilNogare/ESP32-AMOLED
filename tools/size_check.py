# PlatformIO post-build script: keep the firmware inside the OTA budget
# (docs/ARCHITECTURE.md §12): warn above 3.0 MB, fail above 3.5 MB of the
# 4 MB app slot.
import os
import sys

Import("env")  # noqa: F821 (provided by PlatformIO)

WARN_BYTES = 3_000_000
FAIL_BYTES = 3_500_000


def check(source, target, env):
    path = target[0].get_abspath()
    size = os.path.getsize(path)
    pct = size * 100 // (4 * 1024 * 1024)
    print(f"Firmware size: {size} bytes ({pct} % of the 4 MB OTA slot)")
    if size > FAIL_BYTES:
        print(f"ERROR: firmware exceeds the {FAIL_BYTES} byte OTA budget", file=sys.stderr)
        env.Exit(1)
    elif size > WARN_BYTES:
        print(f"WARNING: firmware is above the {WARN_BYTES} byte target")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", check)  # noqa: F821
