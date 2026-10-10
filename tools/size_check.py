# PlatformIO post-build script: keep the firmware inside the OTA budget
# (docs/ARCHITECTURE.md §12): warn above 3.0 MiB, fail above 3.5 MiB. The
# slot size is read from the app0 row of the partition table, so the report
# stays right if partitions.csv changes. "MB" in the docs means MiB here.
import os
import sys

Import("env")  # noqa: F821 (provided by PlatformIO)

MIB = 1024 * 1024
WARN_BYTES = 3 * MIB  # 3.0 MiB
FAIL_BYTES = 7 * MIB // 2  # 3.5 MiB


def parse_size(text):
    """Partition table size: decimal, 0x hex, or with a K/M suffix."""
    text = text.strip().upper()
    mult = 1
    if text.endswith("K"):
        mult, text = 1024, text[:-1]
    elif text.endswith("M"):
        mult, text = MIB, text[:-1]
    return int(text, 0) * mult


def partitions_path(env):
    path = env.subst("$PARTITIONS_TABLE_CSV")
    if path and os.path.isfile(path):
        return path
    name = env.GetProjectOption("board_build.partitions", "partitions.csv")
    return os.path.join(env.subst("$PROJECT_DIR"), name)


def app_slot_bytes(env):
    """Size of the app0 partition, or None if the table can't be read."""
    path = partitions_path(env)
    try:
        with open(path, encoding="utf-8") as f:
            for line in f:
                cols = [c.strip() for c in line.split("#", 1)[0].split(",")]
                if len(cols) >= 5 and cols[0] == "app0":
                    return parse_size(cols[4])
    except (OSError, ValueError) as e:
        print(f"WARNING: can't read the app0 size from {path}: {e}")
        return None
    print(f"WARNING: no app0 partition in {path}")
    return None


def check(source, target, env):
    path = target[0].get_abspath()
    size = os.path.getsize(path)
    slot = app_slot_bytes(env)
    if slot:
        pct = size * 100 // slot
        print(f"Firmware size: {size} bytes ({size / MIB:.2f} MiB, {pct} % of the {slot / MIB:g} MiB OTA slot)")
    else:
        print(f"Firmware size: {size} bytes ({size / MIB:.2f} MiB)")
    if slot and size > slot:
        print(f"ERROR: firmware does not fit the {slot} byte app slot", file=sys.stderr)
        env.Exit(1)
    if size > FAIL_BYTES:
        print(f"ERROR: firmware exceeds the {FAIL_BYTES / MIB:.1f} MiB OTA budget ({FAIL_BYTES} bytes)", file=sys.stderr)
        env.Exit(1)
    elif size > WARN_BYTES:
        print(f"WARNING: firmware is above the {WARN_BYTES / MIB:.1f} MiB target ({WARN_BYTES} bytes)")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", check)  # noqa: F821
