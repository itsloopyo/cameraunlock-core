// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

// The game's executable as the runtime layer reads it: where it lies, byte
// pattern scans over it, and reads that fail instead of faulting.
namespace cameraunlock::re_engine {

struct Exe {
    std::uintptr_t base = 0;
    std::uintptr_t end = 0;

    bool Contains(std::uintptr_t at) const { return at >= base && at < end; }
};

// The executable the layer runs in. cameraunlock_re_engine_game defines it as
// the process's own; a tool that runs the layer over a memory image of a game
// defines it as the image's instead and does not link that target.
const Exe& Game();

// What discovery throws while the engine is still starting: something it builds
// at startup (the VM, the type database, via.Application's stage list) is not
// there yet, and will be.
class NotReady : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// False when any byte of [at, at + size) does not read.
bool SafeRead(std::uintptr_t at, void* out, std::size_t size);

template <class T>
std::optional<T> Read(std::uintptr_t at) {
    T value{};
    if (!SafeRead(at, &value, sizeof(T))) return std::nullopt;
    return value;
}

// A byte pattern, "48 8B 0D ? ? ? ?": hex bytes and ? for any byte.
// Every match in the executable's committed, readable pages.
std::vector<std::uintptr_t> ScanAll(std::string_view pattern);

// The target of the rip-relative operand whose 32-bit displacement is at `disp`.
std::uintptr_t RipTarget(std::uintptr_t disp);

}  // namespace cameraunlock::re_engine
