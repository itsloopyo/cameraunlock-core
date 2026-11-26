#pragma once

#include <cmath>

namespace cameraunlock {
namespace math {

/// Immutable-style 3D vector for position/direction representation.
/// Port of CameraUnlock.Core.Data.Vec3 (C#).
struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    Vec3() = default;
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

    static Vec3 Zero() { return Vec3(0.0f, 0.0f, 0.0f); }
    static Vec3 Forward() { return Vec3(0.0f, 0.0f, 1.0f); }
    static Vec3 Up() { return Vec3(0.0f, 1.0f, 0.0f); }
    static Vec3 Right() { return Vec3(1.0f, 0.0f, 0.0f); }

    float SqrMagnitude() const { return x * x + y * y + z * z; }
    float Magnitude() const { return std::sqrt(x * x + y * y + z * z); }

    Vec3 Normalized() const {
        float sqrMag = x * x + y * y + z * z;
        if (sqrMag < 0.00000001f) return Zero();
        float inv = 1.0f / std::sqrt(sqrMag);
        return Vec3(x * inv, y * inv, z * inv);
    }

    Vec3 operator+(const Vec3& b) const { return Vec3(x + b.x, y + b.y, z + b.z); }
    Vec3 operator-(const Vec3& b) const { return Vec3(x - b.x, y - b.y, z - b.z); }
    Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
    Vec3 operator-() const { return Vec3(-x, -y, -z); }

    static float Dot(const Vec3& a, const Vec3& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    static Vec3 Lerp(const Vec3& a, const Vec3& b, float t) {
        return Vec3(
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t
        );
    }

    static Vec3 Cross(const Vec3& a, const Vec3& b) {
        return Vec3(
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        );
    }
};

inline Vec3 operator*(float s, const Vec3& v) { return Vec3(v.x * s, v.y * s, v.z * s); }

}  // namespace math
}  // namespace cameraunlock
