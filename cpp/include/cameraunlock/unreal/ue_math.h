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

// Unit quaternion of the rotation matrix whose columns are xColumn, yColumn and
// zColumn: the world directions the rotation takes +X, +Y and +Z to. In UE's
// frame those are the forward, right and up axes; a backend whose camera keeps
// its axes in another frame passes the ones its own +X, +Y and +Z take. The
// columns must be unit length, at right angles, with zColumn = xColumn x yColumn:
// scaled or mirrored axes give a wrong rotation, not a scaled one. Only the
// result is normalised. Shepperd's method.
inline FQuat4d QuatFromAxes(const FVector& xColumn, const FVector& yColumn, const FVector& zColumn) {
    const double m00 = xColumn.X, m10 = xColumn.Y, m20 = xColumn.Z;
    const double m01 = yColumn.X, m11 = yColumn.Y, m21 = yColumn.Z;
    const double m02 = zColumn.X, m12 = zColumn.Y, m22 = zColumn.Z;
    const double tr = m00 + m11 + m22;
    FQuat4d q;
    if (tr > 0) {
        const double s = std::sqrt(tr + 1.0) * 2;
        q = {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25 * s};
    } else if (m00 > m11 && m00 > m22) {
        const double s = std::sqrt(1.0 + m00 - m11 - m22) * 2;
        q = {0.25 * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
    } else if (m11 > m22) {
        const double s = std::sqrt(1.0 + m11 - m00 - m22) * 2;
        q = {(m01 + m10) / s, 0.25 * s, (m12 + m21) / s, (m02 - m20) / s};
    } else {
        const double s = std::sqrt(1.0 + m22 - m00 - m11) * 2;
        q = {(m02 + m20) / s, (m12 + m21) / s, 0.25 * s, (m10 - m01) / s};
    }
    const double n = std::sqrt(q.X * q.X + q.Y * q.Y + q.Z * q.Z + q.W * q.W);
    return {q.X / n, q.Y / n, q.Z / n, q.W / n};
}

// `q` at unit length. A zero quaternion has no direction and comes back NaN.
inline FQuat4d QuatNormalized(const FQuat4d& q) {
    const double n = std::sqrt(q.X * q.X + q.Y * q.Y + q.Z * q.Z + q.W * q.W);
    return {q.X / n, q.Y / n, q.Z / n, q.W / n};
}

// `q`'s turn through `multiplier` times its angle about the same axis, the
// shorter way round: q and -q are the same turn, and the one with W >= 0 is
// scaled. No turn at all stays the identity.
inline FQuat4d QuatScaleTurn(const FQuat4d& q, double multiplier) {
    const FQuat4d p = q.W < 0 ? FQuat4d{-q.X, -q.Y, -q.Z, -q.W} : q;
    const double sinHalf = std::sqrt(p.X * p.X + p.Y * p.Y + p.Z * p.Z);
    if (sinHalf < 1e-12) return {0, 0, 0, 1};
    const double half = std::atan2(sinHalf, p.W) * multiplier;
    const double k = std::sin(half) / sinHalf;
    return {p.X * k, p.Y * k, p.Z * k, std::cos(half)};
}

// Spherical interpolation from `a` (t 0) to `b` (t 1), the shorter way round.
// Nearly equal turns are blended linearly and normalised, where slerp's
// division by sin(angle) loses its precision.
inline FQuat4d QuatSlerp(const FQuat4d& a, FQuat4d b, double t) {
    double dot = a.X * b.X + a.Y * b.Y + a.Z * b.Z + a.W * b.W;
    if (dot < 0.0) {
        b = {-b.X, -b.Y, -b.Z, -b.W};
        dot = -dot;
    }
    if (dot > 0.9995)
        return QuatNormalized({a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t, a.Z + (b.Z - a.Z) * t, a.W + (b.W - a.W) * t});
    const double theta = std::acos(dot);
    const double sa = std::sin((1.0 - t) * theta) / std::sin(theta);
    const double sb = std::sin(t * theta) / std::sin(theta);
    return {a.X * sa + b.X * sb, a.Y * sa + b.Y * sb, a.Z * sa + b.Z * sb, a.W * sa + b.W * sb};
}

// The turn that takes unit `fromUnit` onto unit `toUnit` by the shortest way.
// A half turn has no one axis to turn about, and gives the identity.
inline FQuat4d QuatShortestArc(const FVector& fromUnit, const FVector& toUnit) {
    const double dot = fromUnit.X * toUnit.X + fromUnit.Y * toUnit.Y + fromUnit.Z * toUnit.Z;
    if (dot < -0.999999) return {0.0, 0.0, 0.0, 1.0};
    const FVector axis{fromUnit.Y * toUnit.Z - fromUnit.Z * toUnit.Y, fromUnit.Z * toUnit.X - fromUnit.X * toUnit.Z,
                       fromUnit.X * toUnit.Y - fromUnit.Y * toUnit.X};
    return QuatNormalized({axis.X, axis.Y, axis.Z, 1.0 + dot});
}

// The rotation whose +X is along `forward` and whose +Z is as near `up` as that
// allows (+Y = Z x X, so in UE's frame the right axis). Neither need be unit
// length. With no forward, or an `up` along it, there is no such rotation and
// the identity comes back.
inline FQuat4d QuatFromForwardUp(const FVector& forward, const FVector& up) {
    const auto cross = [](const FVector& a, const FVector& b) {
        return FVector{a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X};
    };
    const auto unit = [](const FVector& v, FVector& out) {
        const double length = std::sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z);
        if (!(length > 1e-9)) return false;
        out = {v.X / length, v.Y / length, v.Z / length};
        return true;
    };
    FVector x{}, y{};
    if (!unit(forward, x) || !unit(cross(up, x), y)) return {0.0, 0.0, 0.0, 1.0};
    return QuatFromAxes(x, y, cross(x, y));
}

// The rotator whose forward (+X), right (+Y) and up (+Z) axes are these, in
// UE's FRotationMatrix convention: the same rotation QuatFromEulerDeg builds.
// Pitch is read off forward alone, so it holds at the poles, where yaw and roll
// are whatever atan2 makes of the near-zero parts.
inline FRotator RotatorFromAxes(const FVector& forward, const FVector& right, const FVector& up) {
    constexpr double kRadPerDeg = 3.14159265358979323846 / 180.0;
    FRotator out;
    out.Pitch = std::asin(std::fmax(-1.0, std::fmin(1.0, forward.Z))) / kRadPerDeg;
    out.Yaw = std::atan2(forward.Y, forward.X) / kRadPerDeg;
    out.Roll = std::atan2(-right.Z, up.Z) / kRadPerDeg;
    return out;
}

// The way back: the forward, right and up axes of `rotator`.
inline void AxesFromRotator(const FRotator& rotator, FVector& forward, FVector& right, FVector& up) {
    constexpr double kRadPerDeg = 3.14159265358979323846 / 180.0;
    const double sp = std::sin(rotator.Pitch * kRadPerDeg), cp = std::cos(rotator.Pitch * kRadPerDeg);
    const double sy = std::sin(rotator.Yaw * kRadPerDeg), cy = std::cos(rotator.Yaw * kRadPerDeg);
    const double sr = std::sin(rotator.Roll * kRadPerDeg), cr = std::cos(rotator.Roll * kRadPerDeg);
    forward = {cp * cy, cp * sy, sp};
    right = {sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp};
    up = {-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp};
}

}  // namespace cameraunlock::unreal
