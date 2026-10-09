# PlatformIO pre-build script: APP_VERSION from `git describe`, so the boot
# log, the System app and the OTA screen show exactly which build is running.
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
env.Append(CPPDEFINES=[("APP_VERSION", env.StringifyMacro(version))])  # noqa: F821
print(f"APP_VERSION = {version}")
