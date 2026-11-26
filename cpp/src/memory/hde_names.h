// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

// The vendored hde32 and hde64 are compiled into cameraunlock_hooks under
// these names, so they never meet the copy inside a MinHook target (core's, or
// one a consumer supplies), which builds only its own architecture's.
#define hde32_disasm cameraunlock_hde32_disasm
#define hde32_table cameraunlock_hde32_table
#define hde64_disasm cameraunlock_hde64_disasm
#define hde64_table cameraunlock_hde64_table
