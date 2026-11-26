#pragma once

#include <cstdint>
#include <cstddef>

namespace cameraunlock::discovery {

// Classification of a float group found in memory
enum class FloatClass {
    Unknown,
    Position,      // 3 floats that look like world coordinates (+ optional w=1)
    Angle,         // 3 floats in plausible Euler angle range (-360..360)
    FOV,           // Single float in 20..150 range
    Quaternion,    // 4 floats with length ~1.0
};

struct FloatGroup {
    size_t offset;          // byte offset from region start
    FloatClass type;
    int count;              // number of floats in group (3 for position/angle, 4 for quat, 1 for FOV)
    float values[4];        // the actual values
};

struct LayoutReport {
    static constexpr int kMaxGroups = 32;
    FloatGroup groups[kMaxGroups];
    int group_count;
};

// Classify floats in a raw memory region.
// Reads float values at 4-byte alignment, identifies groups.
// region: must be readable (caller ensures via SEH if needed)
// size: bytes to scan (should be <= ~512 to stay within one allocation)
LayoutReport ClassifyMemoryRegion(const void* region, size_t size);

} // namespace cameraunlock::discovery
