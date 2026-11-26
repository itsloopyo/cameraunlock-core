#pragma once

#include <cmath>

// UE5 POD math types and the quaternion/rotator conversions a UE C++ hook
// needs. All doubles: UE 5.0+ ships with Large World Coordinates on by
// default, so the canonical FVector/FRotator are 3-double (FVector3d /
// FRotator3d, 24 bytes each). Declaring them as floats overflows the
// engine's out-params and decodes adjacent bytes as garbage.
namespace cameraunlock::unreal {

struct FVector  { double X, Y, Z; };
struct FRotator { double Pitch, Yaw, Roll; };
struct FQuat4d  { double X, Y, Z, W; };

inline FQuat4d QuatMul(const FQuat4d& a, const FQuat4d& b) {
    return FQuat4d{
        a.W*b.X + a.X*b.W + a.Y*b.Z - a.Z*b.Y,
        a.W*b.Y - a.X*b.Z + a.Y*b.W + a.Z*b.X,
        a.W*b.Z + a.X*b.Y - a.Y*b.X + a.Z*b.W,
        a.W*b.W - a.X*b.X - a.Y*b.Y - a.Z*b.Z
    };
}

// Rotator to quaternion, in UE5's rotator convention: Pitch about Y, Yaw
// about Z, Roll about X, in degrees, with UE's handedness and sign for each.
// The convention is what the engine's own values are expressed in, so a hook
// that reads or writes an FRotator has to match it exactly or the camera it
// hands back is not the one the game asked for. Applied roll first, then
// pitch, then yaw; pitch and roll turn the opposite way to a right-handed
// rotation about their axes.
inline FQuat4d QuatFromEulerDeg(double pitchDeg, double yawDeg, double rollDeg) {
    constexpr double kHalfRadPerDeg = 3.14159265358979323846 / 360.0;
    const double p = pitchDeg * kHalfRadPerDeg;
    const double y = yawDeg * kHalfRadPerDeg;
    const double r = rollDeg * kHalfRadPerDeg;
    const FQuat4d yaw{0.0, 0.0, std::sin(y), std::cos(y)};
    const FQuat4d pitch{0.0, -std::sin(p), 0.0, std::cos(p)};
    const FQuat4d roll{-std::sin(r), 0.0, 0.0, std::cos(r)};
    return QuatMul(QuatMul(yaw, pitch), roll);
}

inline FQuat4d QuatInv(const FQuat4d& q) {
    return FQuat4d{-q.X, -q.Y, -q.Z, q.W};
}

// The inverse, in the same convention. Near +/-90 degrees of pitch, yaw and
// roll stop being separable: past a fixed threshold the engine pins pitch to
// the pole and folds the remaining twist into roll. Round-tripping a rotator
// through QuatFromEulerDeg and back has to land on the value the engine would
// have produced, so the threshold is the engine's.
inline FRotator QuatToRotator(const FQuat4d& q) {
    constexpr double kDegPerRad = 180.0 / 3.14159265358979323846;
    constexpr double kPoleThreshold = 0.4999995;
    auto wrap180 = [](double deg) {
        while (deg > 180.0)  deg -= 360.0;
        while (deg < -180.0) deg += 360.0;
        return deg;
    };
    const double halfSinPitch = q.Z*q.X - q.W*q.Y;
    const double yaw = std::atan2(2.0 * (q.W*q.Z + q.X*q.Y),
                                  1.0 - 2.0 * (q.Y*q.Y + q.Z*q.Z)) * kDegPerRad;
    if (std::abs(halfSinPitch) > kPoleThreshold) {
        const double pole = halfSinPitch > 0.0 ? 1.0 : -1.0;
        const double twist = 2.0 * std::atan2(q.X, q.W) * kDegPerRad;
        return FRotator{90.0 * pole, yaw, wrap180(pole * yaw - twist)};
    }
    const double roll = std::atan2(-2.0 * (q.W*q.X + q.Y*q.Z),
                                   1.0 - 2.0 * (q.X*q.X + q.Y*q.Y)) * kDegPerRad;
    return FRotator{std::asin(2.0 * halfSinPitch) * kDegPerRad, yaw, roll};
}

// Rotate a vector by a unit quaternion: v' = q * v * q^-1, expanded.
inline FVector QuatRotateVec(const FQuat4d& q, const FVector& v) {
    const double xx = q.X*q.X, yy = q.Y*q.Y, zz = q.Z*q.Z;
    const double xy = q.X*q.Y, xz = q.X*q.Z, yz = q.Y*q.Z;
    const double wx = q.W*q.X, wy = q.W*q.Y, wz = q.W*q.Z;
    FVector r{};
    r.X = (1.0 - 2.0*(yy + zz))*v.X + 2.0*(xy - wz)*v.Y       + 2.0*(xz + wy)*v.Z;
    r.Y = 2.0*(xy + wz)*v.X       + (1.0 - 2.0*(xx + zz))*v.Y + 2.0*(yz - wx)*v.Z;
    r.Z = 2.0*(xz - wy)*v.X       + 2.0*(yz + wx)*v.Y         + (1.0 - 2.0*(xx + yy))*v.Z;
    return r;
}

}  // namespace cameraunlock::unreal
