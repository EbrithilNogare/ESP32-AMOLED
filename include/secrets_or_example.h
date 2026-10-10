#pragma once

// Credentials: include/secrets.h when it exists (gitignored), otherwise the
// placeholders in include/secrets.example.h, so CI and fresh clones build.
// Include this instead of either file directly.
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif
