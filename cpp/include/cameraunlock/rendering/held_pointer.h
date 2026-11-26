#pragma once

// Which of a set of candidate pointers an object holds.
//
// The D3D12 overlay uses it to find the command queue a swap chain was created
// against: DXGI keeps that queue's pointer inside the swap chain object and
// offers no call that returns it, so the object's own memory is the evidence.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace cameraunlock::rendering {

inline constexpr int kHeldNone = -1;
inline constexpr int kHeldAmbiguous = -2;

// Reads `bytes` of `object` at pointer-aligned offsets and answers the index of
// the one candidate found there. kHeldNone when none is, kHeldAmbiguous when two
// different candidates are: an object holding both says nothing about which one
// it submits through. `offset`, when given, receives where the answer was found.
// Null candidates are skipped. The caller guarantees the bytes are readable.
inline int FindHeldPointer(const void* object, std::size_t bytes, const void* const* candidates,
                           int count, std::size_t* offset = nullptr) {
    int found = kHeldNone;
    const auto* base = static_cast<const unsigned char*>(object);
    for (std::size_t at = 0; at + sizeof(void*) <= bytes; at += sizeof(void*)) {
        const void* value = nullptr;
        std::memcpy(&value, base + at, sizeof(value));
        if (value == nullptr) continue;
        for (int i = 0; i < count; ++i) {
            if (candidates[i] != value) continue;
            if (found != kHeldNone && found != i) return kHeldAmbiguous;
            if (found == kHeldNone && offset) *offset = at;
            found = i;
        }
    }
    return found;
}

}  // namespace cameraunlock::rendering
