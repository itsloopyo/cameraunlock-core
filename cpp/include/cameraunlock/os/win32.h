#pragma once

// Windows for core's Win32 code. On Windows it is the system's <windows.h>. The WebAssembly
// build, which defines CAMERAUNLOCK_WIN32_HOST, has no Windows headers: there the functions core
// calls are imports, declared in win32_host.h, which the Java host behind core's jar implements
// with the same calls into Windows' own DLLs (docs/core-jar.md). CAMERAUNLOCK_WIN32 is 1 in both,
// and guards the code that calls Windows.

#if defined(_WIN32) || defined(CAMERAUNLOCK_WIN32_HOST)
#define CAMERAUNLOCK_WIN32 1
#else
#define CAMERAUNLOCK_WIN32 0
#endif

#if defined(CAMERAUNLOCK_WIN32_HOST)
#include "cameraunlock/os/win32_host.h"
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
