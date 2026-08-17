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

    /// Length below which a quaternion is treated as degenerate and replaced with
    /// identity. Matches QuaternionUtils.NormalizationEpsilon in C#, which tests
    /// lengthSq against its square - this side used 1e-6f, so a quaternion of length
    /// 1e-5 normalised here and returned identity there.
    static constexpr float kNormalizationEpsilon = 0.0001f;

    /// Returns a unit-length copy of this quaternion.
    Quat4 Normalized() const {
        float lengthSq = x * x + y * y + z * z + w * w;
        if (lengthSq < kNormalizationEpsilon * kNormalizationEpsilon) return Identity();
        float inv = 1.0f / std::sqrt(lengthSq);
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
        // sin(89.9 deg) is 0.9999985, so the general branch keeps every angle where it
        // is still well-conditioned.
        constexpr float kGimbalLockSinThreshold = 0.9999995f;

        float sinPitch = 2.0f * (w * x - y * z);

        // Switch to the lock branch slightly BEFORE the exact singularity, and use the
        // row-0 form there. At pitch = 90 the general formula's two atan2 arguments are
        // identically zero for every yaw - 2(xz + wy) and 1 - 2(x^2 + y^2) both cancel
        // exactly - so it returns atan2(0, 0) = 0 and discards the rotation. Testing
        // sinPitch >= 1 never caught that: accumulated float error leaves sinPitch a
        // shade under 1 for a quaternion built at exactly 90 degrees, so the degenerate
        // general branch ran anyway. Matches QuaternionUtils.ToEulerYXZ in C#.
        if (sinPitch >= kGimbalLockSinThreshold) {
            pitch = 90.0f;
            yaw = std::atan2(2.0f * (x * y - w * z), 1.0f - 2.0f * (y * y + z * z)) * kRadToDeg;
            roll = 0.0f;
        } else if (sinPitch <= -kGimbalLockSinThreshold) {
            pitch = -90.0f;
            yaw = std::atan2(-2.0f * (x * y - w * z), 1.0f - 2.0f * (y * y + z * z)) * kRadToDeg;
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
