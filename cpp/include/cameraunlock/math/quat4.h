#pragma once

#include <cmath>
#include "cameraunlock/math/vec3.h"

namespace cameraunlock {
namespace math {

/// Immutable-style quaternion for rotation representation (xyzw component order).
/// Port of CameraUnlock.Core.Data.Quat4 (C#).
struct Quat4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    Quat4() = default;
    Quat4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

    static Quat4 Identity() { return Quat4(0.0f, 0.0f, 0.0f, 1.0f); }

    Quat4 Negated() const { return Quat4(-x, -y, -z, -w); }

    /// Returns the conjugate/inverse of a unit quaternion.
    Quat4 Inverse() const { return Quat4(-x, -y, -z, w); }

    float Dot(const Quat4& other) const {
        return x * other.x + y * other.y + z * other.z + w * other.w;
    }

    /// Rotates a vector by this quaternion: q * v * q^-1 (optimized).
    Vec3 Rotate(const Vec3& v) const {
        float x2 = x + x;
        float y2 = y + y;
        float z2 = z + z;

        float xx2 = x * x2;
        float yy2 = y * y2;
        float zz2 = z * z2;
        float xy2 = x * y2;
        float xz2 = x * z2;
        float yz2 = y * z2;
        float wx2 = w * x2;
        float wy2 = w * y2;
        float wz2 = w * z2;

        return Vec3(
            (1.0f - yy2 - zz2) * v.x + (xy2 - wz2) * v.y + (xz2 + wy2) * v.z,
            (xy2 + wz2) * v.x + (1.0f - xx2 - zz2) * v.y + (yz2 - wx2) * v.z,
            (xz2 - wy2) * v.x + (yz2 + wx2) * v.y + (1.0f - xx2 - yy2) * v.z
        );
    }

    /// Multiplies two quaternions: this * b.
    Quat4 Multiply(const Quat4& b) const {
        return Quat4(
            w * b.x + x * b.w + y * b.z - z * b.y,
            w * b.y - x * b.z + y * b.w + z * b.x,
            w * b.z + x * b.y - y * b.x + z * b.w,
            w * b.w - x * b.x - y * b.y - z * b.z
        );
    }

    Quat4 operator*(const Quat4& b) const { return Multiply(b); }

    /// Returns a unit-length copy of this quaternion.
    Quat4 Normalized() const {
        float len = std::sqrt(x * x + y * y + z * z + w * w);
        if (len < 1e-6f) return Identity();
        float inv = 1.0f / len;
        return Quat4(x * inv, y * inv, z * inv, w * inv);
    }

    /// Creates a quaternion from YXZ Euler angles (yaw, pitch, roll in degrees).
    /// Matches C# QuaternionUtils.FromYawPitchRoll.
    static Quat4 FromYawPitchRoll(float yawDeg, float pitchDeg, float rollDeg) {
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
        float halfYaw = yawDeg * kDegToRad * 0.5f;
        float halfPitch = pitchDeg * kDegToRad * 0.5f;
        float halfRoll = rollDeg * kDegToRad * 0.5f;

        float sy = std::sin(halfYaw);
        float cy = std::cos(halfYaw);
        float sp = std::sin(halfPitch);
        float cp = std::cos(halfPitch);
        float sr = std::sin(halfRoll);
        float cr = std::cos(halfRoll);

        return Quat4(
            cy * sp * cr + sy * cp * sr,
            sy * cp * cr - cy * sp * sr,
            cy * cp * sr - sy * sp * cr,
            cy * cp * cr + sy * sp * sr
        );
    }

    /// Decomposes this quaternion into YXZ Euler angles (yaw, pitch, roll in degrees).
    /// Matches C# QuaternionUtils.ToEulerYXZ. Handles gimbal lock at ±90° pitch.
    void ToEulerYXZ(float& yaw, float& pitch, float& roll) const {
        constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;

        float sinPitch = 2.0f * (w * x - y * z);

        if (sinPitch >= 1.0f) {
            pitch = 90.0f;
            yaw = std::atan2(2.0f * (x * z + w * y), 1.0f - 2.0f * (x * x + y * y)) * kRadToDeg;
            roll = 0.0f;
        } else if (sinPitch <= -1.0f) {
            pitch = -90.0f;
            yaw = std::atan2(2.0f * (x * z + w * y), 1.0f - 2.0f * (x * x + y * y)) * kRadToDeg;
            roll = 0.0f;
        } else {
            pitch = std::asin(sinPitch) * kRadToDeg;
            yaw = std::atan2(2.0f * (x * z + w * y), 1.0f - 2.0f * (x * x + y * y)) * kRadToDeg;
            roll = std::atan2(2.0f * (x * y + w * z), 1.0f - 2.0f * (x * x + z * z)) * kRadToDeg;
        }
    }

    /// Spherical linear interpolation. Takes the shortest path through quaternion space.
    /// Matches C# QuaternionUtils.Slerp.
    static Quat4 Slerp(const Quat4& a, const Quat4& b, float t) {
        float dot = a.Dot(b);

        // Flip sign to take shorter path
        float sign = 1.0f;
        if (dot < 0.0f) {
            sign = -1.0f;
            dot = -dot;
        }

        // Near-identical quaternions: normalized lerp to avoid division by ~0
        if (dot > 0.9995f) {
            return Quat4(
                a.x + t * (sign * b.x - a.x),
                a.y + t * (sign * b.y - a.y),
                a.z + t * (sign * b.z - a.z),
                a.w + t * (sign * b.w - a.w)
            ).Normalized();
        }

        float theta = std::acos(dot);
        float sinTheta = std::sin(theta);
        float invSinTheta = 1.0f / sinTheta;
        float wa = std::sin((1.0f - t) * theta) * invSinTheta;
        float wb = sign * std::sin(t * theta) * invSinTheta;

        return Quat4(
            wa * a.x + wb * b.x,
            wa * a.y + wb * b.y,
            wa * a.z + wb * b.z,
            wa * a.w + wb * b.w
        );
    }
};

}  // namespace math
}  // namespace cameraunlock
