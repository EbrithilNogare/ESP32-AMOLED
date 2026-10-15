#pragma once

// APP_VERSION: the git-describe version that tools/version.py writes to a
// generated header in the build directory. Include this only where the version
// is shown, so a new commit recompiles just those files.
#if __has_include("app_version.h")
#include "app_version.h"
#else
#define APP_VERSION "0.0.0-dev"  // sim / native builds, which don't run the script
#endif
