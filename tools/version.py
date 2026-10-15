# PlatformIO pre-build script: APP_VERSION from `git describe`, so the boot
# log, the System app and the OTA screen show exactly which build is running.
#
# Written to a generated header (included only through include/version.h), not
# passed as a -D flag: a define on the command line changes every compile
# command whenever the version does, and SCons then rebuilds the whole tree
# (Arduino core, LVGL, every library) after each commit.
import os
import subprocess

Import("env")  # noqa: F821 (provided by PlatformIO)


def describe():
    try:
        out = subprocess.run(
            ["git", "describe", "--tags", "--always", "--dirty"],
            cwd=env.subst("$PROJECT_DIR"),  # noqa: F821
            capture_output=True,
            text=True,
            check=True,
        )
        return out.stdout.strip() or "unknown"
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


version = describe()
gen_dir = os.path.join(env.subst("$BUILD_DIR"), "generated")  # noqa: F821
header = os.path.join(gen_dir, "app_version.h")
content = f'#pragma once\n#define APP_VERSION "{version}"\n'
os.makedirs(gen_dir, exist_ok=True)
try:
    with open(header) as f:
        unchanged = f.read() == content
except OSError:
    unchanged = False
if not unchanged:  # rewrite only on change, so the two users recompile only then
    with open(header, "w") as f:
        f.write(content)
env.Append(CPPPATH=[gen_dir])  # noqa: F821
print(f"APP_VERSION = {version}")
