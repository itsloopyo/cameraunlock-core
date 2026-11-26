// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
// hde32 for DecodeX86 in every build. hde32.c compiles only when it sees an x86
// target; the decoder is plain table-driven C with nothing of the target in
// it, so an x64 build defines the macro it looks for. <string.h> and pstdint.h's
// <windows.h> are included first, so no system header sees the macro.
#include <string.h>
#include <windows.h>

#include "hde_names.h"

#if !defined(_M_IX86) && !defined(__i386__)
#define _M_IX86 600
#endif
#include "../../../vendor/minhook/src/hde/hde32.c"
