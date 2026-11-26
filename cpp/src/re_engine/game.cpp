// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#include <Windows.h>

#include <cameraunlock/re_engine/exe.h>

namespace cameraunlock::re_engine {

const Exe& Game() {
    static const Exe exe = [] {
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        return Exe{base, base + nt->OptionalHeader.SizeOfImage};
    }();
    return exe;
}

}  // namespace cameraunlock::re_engine
