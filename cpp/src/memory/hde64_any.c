// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
// hde64 for DecodeX64 in every build. hde64.c compiles only when it sees an x64
// target; the decoder is plain table-driven C with nothing of the target in
// it, so an x86 build defines the macro it looks for. <string.h> and pstdint.h's
// <windows.h> are included first, so no system header sees the macro.
#include <string.h>
#include <windows.h>

#include "hde_names.h"

#if !defined(_M_X64) && !defined(__x86_64__)
#define _M_X64 100
#endif
#include "../../../vendor/minhook/src/hde/hde64.c"
