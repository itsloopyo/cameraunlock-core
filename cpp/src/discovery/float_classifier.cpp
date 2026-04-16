#include <cameraunlock/discovery/float_classifier.h>

#include <cmath>
#include <cstring>
#include <vector>

namespace cameraunlock::discovery {

namespace {

bool IsPlausibleFloat(float f) {
    return std::isfinite(f) && std::fabsf(f) < 1e10f;
}

bool IsAngleLike(float f) {
    return std::isfinite(f) && std::fabsf(f) <= 360.0f;
}

} // namespace

LayoutReport ClassifyMemoryRegion(const void* region, size_t size) {
    LayoutReport report{};
    report.group_count = 0;

    if (!region || size < 16) return report;

    const size_t floatCount = size / sizeof(float);
    const float* floats = static_cast<const float*>(region);

    // Track which offsets have been claimed by a group
    std::vector<bool> claimed(floatCount, false);

    // Pass 1: Find quaternions (4 consecutive floats with unit length)
    for (size_t i = 0; i + 3 < floatCount && report.group_count < LayoutReport::kMaxGroups; i++) {
        if (claimed[i]) continue;
        float a = floats[i], b = floats[i+1], c = floats[i+2], d = floats[i+3];
        if (!IsPlausibleFloat(a) || !IsPlausibleFloat(b) ||
            !IsPlausibleFloat(c) || !IsPlausibleFloat(d)) continue;

        float len2 = a*a + b*b + c*c + d*d;
        if (std::fabsf(len2 - 1.0f) < 0.01f) {
            // At least one component should be non-trivial (not just 0,0,0,1)
            int nonzero = 0;
            if (std::fabsf(a) > 0.001f) nonzero++;
            if (std::fabsf(b) > 0.001f) nonzero++;
            if (std::fabsf(c) > 0.001f) nonzero++;
            if (std::fabsf(d) > 0.001f) nonzero++;
            if (nonzero >= 2) {
                auto& g = report.groups[report.group_count++];
                g.offset = i * sizeof(float);
                g.type = FloatClass::Quaternion;
                g.count = 4;
                g.values[0] = a; g.values[1] = b; g.values[2] = c; g.values[3] = d;
                claimed[i] = claimed[i+1] = claimed[i+2] = claimed[i+3] = true;
            }
        }
    }

    // Pass 2: Find positions (3 floats with reasonable world coords, w=1.0 after)
    for (size_t i = 0; i + 3 < floatCount && report.group_count < LayoutReport::kMaxGroups; i++) {
        if (claimed[i] || claimed[i+1] || claimed[i+2]) continue;
        float x = floats[i], y = floats[i+1], z = floats[i+2];
        if (!IsPlausibleFloat(x) || !IsPlausibleFloat(y) || !IsPlausibleFloat(z)) continue;

        // At least one coord has magnitude > 1 (not a normalized vector)
        bool hasLarge = std::fabsf(x) > 1.0f || std::fabsf(y) > 1.0f || std::fabsf(z) > 1.0f;
        // All coords in reasonable world range
        bool inRange = std::fabsf(x) < 100000.0f && std::fabsf(y) < 100000.0f && std::fabsf(z) < 100000.0f;
        // w=1.0 at the next float (homogeneous coordinate)
        bool hasW1 = (i + 3 < floatCount) && std::fabsf(floats[i+3] - 1.0f) < 0.001f;

        if (hasLarge && inRange && hasW1) {
            auto& g = report.groups[report.group_count++];
            g.offset = i * sizeof(float);
            g.type = FloatClass::Position;
            g.count = 3;
            g.values[0] = x; g.values[1] = y; g.values[2] = z;
            claimed[i] = claimed[i+1] = claimed[i+2] = true;
            if (hasW1) claimed[i+3] = true;
        }
    }

    // Pass 3: Find Euler angle groups (3 consecutive floats in angle range, not all zero)
    for (size_t i = 0; i + 2 < floatCount && report.group_count < LayoutReport::kMaxGroups; i++) {
        if (claimed[i] || claimed[i+1] || claimed[i+2]) continue;
        float a = floats[i], b = floats[i+1], c = floats[i+2];
        if (!IsAngleLike(a) || !IsAngleLike(b) || !IsAngleLike(c)) continue;

        // At least one must be nonzero
        if (std::fabsf(a) < 0.001f && std::fabsf(b) < 0.001f && std::fabsf(c) < 0.001f) continue;

        // Distinguish from small position values: at least one should be > 1 degree
        // or all should be < 360 and not look like a tiny position
        // Reject identity matrix basis vectors: groups where all floats are
        // exactly 0.0 or ±1.0 (e.g., (1,0,0), (0,1,0), (0,0,1))
        bool allTrivial = true;
        float vals[3] = {a, b, c};
        for (int k = 0; k < 3; k++) {
            float absv = std::fabsf(vals[k]);
            if (absv > 0.001f && std::fabsf(absv - 1.0f) > 0.001f) {
                allTrivial = false;
                break;
            }
        }
        if (allTrivial) continue;  // skip — this is a matrix row, not angles

        bool looksLikeAngle = (std::fabsf(a) > 0.5f || std::fabsf(b) > 0.5f || std::fabsf(c) > 0.5f)
                           && (std::fabsf(a) < 360.0f && std::fabsf(b) < 360.0f && std::fabsf(c) < 360.0f);

        if (looksLikeAngle) {
            auto& g = report.groups[report.group_count++];
            g.offset = i * sizeof(float);
            g.type = FloatClass::Angle;
            g.count = 3;
            g.values[0] = a; g.values[1] = b; g.values[2] = c;
            claimed[i] = claimed[i+1] = claimed[i+2] = true;
        }
    }

    // Pass 4: Find FOV (single float in 20..150 range, not already claimed)
    for (size_t i = 0; i < floatCount && report.group_count < LayoutReport::kMaxGroups; i++) {
        if (claimed[i]) continue;
        float f = floats[i];
        if (f >= 20.0f && f <= 150.0f) {
            // Check neighbors aren't also in this range (avoid claiming part of a vector)
            bool neighborsFOV = false;
            if (i > 0 && !claimed[i-1] && floats[i-1] >= 20.0f && floats[i-1] <= 150.0f)
                neighborsFOV = true;
            if (i+1 < floatCount && !claimed[i+1] && floats[i+1] >= 20.0f && floats[i+1] <= 150.0f)
                neighborsFOV = true;

            if (!neighborsFOV) {
                auto& g = report.groups[report.group_count++];
                g.offset = i * sizeof(float);
                g.type = FloatClass::FOV;
                g.count = 1;
                g.values[0] = f;
                claimed[i] = true;
            }
        }
    }

    return report;
}

} // namespace cameraunlock::discovery
