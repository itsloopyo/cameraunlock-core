#pragma once

#include <cstdint>
#include <chrono>
#include "cameraunlock/math/vec3.h"

namespace cameraunlock {

/// Immutable 3DOF position data with timestamp.
/// Port of CameraUnlock.Core.Data.PositionData (C#).
struct PositionData {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    int64_t timestamp_us = 0;  // Microseconds since epoch

    PositionData() = default;

    PositionData(float x, float y, float z, int64_t timestamp_us)
        : x(x), y(y), z(z), timestamp_us(timestamp_us) {}

    PositionData(float x, float y, float z)
        : x(x), y(y), z(z), timestamp_us(CurrentTimestamp()) {}

    bool IsValid() const { return timestamp_us != 0; }

    static PositionData Zero() {
        return PositionData(0.0f, 0.0f, 0.0f, CurrentTimestamp());
    }

    math::Vec3 ToVec3() const { return math::Vec3(x, y, z); }

    PositionData SubtractOffset(const PositionData& offset) const {
        return PositionData(x - offset.x, y - offset.y, z - offset.z, timestamp_us);
    }

    static int64_t CurrentTimestamp() {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()).count();
    }
};

}  // namespace cameraunlock
