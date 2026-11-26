// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <algorithm>
#include <cmath>

#include "cameraunlock/unreal/ue_math.h"

// The turn a carried light takes from the head, as rotations rather than angles:
// for a mod that has the clean and the drawn view as quaternions or as axis rows
// and scales the head's turn between them (ScaleHeadAngle's axis-angle shape, in
// head_follow_light.h, which says why the light leads the view and by how much).
namespace cameraunlock::effects {

/// The world turn that carries the clean view `multiplier` times as far as the
/// head turned it to the posed view, about the same axis: a light whose world
/// rotation is turned by this leads the eyes by the multiplier. Both views are
/// world rotations in one frame; the turn is the head's in the clean view's own
/// frame, clean^-1 * posed, scaled by QuatScaleTurn and laid back on the world,
/// so it is the same turn whichever frame the engine keeps its rotations in.
inline unreal::FQuat4d FollowTurn(const unreal::FQuat4d& clean, const unreal::FQuat4d& posed, double multiplier) {
    const unreal::FQuat4d head = unreal::QuatMul(unreal::QuatInv(clean), posed);
    const unreal::FQuat4d led = unreal::QuatMul(clean, unreal::QuatScaleTurn(head, multiplier));
    return unreal::QuatNormalized(unreal::QuatMul(led, unreal::QuatInv(clean)));
}

/// The same, with the light's turn held to `maxTurnRad` or the head's own turn,
/// whichever is larger: a far head turn at a large multiplier would otherwise put
/// the beam behind the player (hpl's held lantern holds it to a quarter turn).
/// With a multiplier above 1, a head turned further than `maxTurnRad` is followed
/// 1:1.
inline unreal::FQuat4d FollowTurn(const unreal::FQuat4d& clean, const unreal::FQuat4d& posed, double multiplier,
                                  double maxTurnRad) {
    const unreal::FQuat4d head = unreal::QuatMul(unreal::QuatInv(clean), posed);
    const double sinHalf = std::sqrt(head.X * head.X + head.Y * head.Y + head.Z * head.Z);
    const double angle = 2.0 * std::atan2(sinHalf, std::fabs(head.W));
    const double most = (std::max)(angle, maxTurnRad);
    return FollowTurn(clean, posed, angle * multiplier > most ? most / angle : multiplier);
}

/// The angle, in degrees, of the turn from one set of axes to the other. Each set
/// is three rows of four floats (the fourth left alone), in one frame and order.
inline double BasisTurnDegrees(const float (*a)[4], const float (*b)[4]) {
    double trace = 0;
    for (int i = 0; i < 3; ++i) {
        const double na = std::sqrt(double(a[i][0]) * a[i][0] + double(a[i][1]) * a[i][1] + double(a[i][2]) * a[i][2]);
        const double nb = std::sqrt(double(b[i][0]) * b[i][0] + double(b[i][1]) * b[i][1] + double(b[i][2]) * b[i][2]);
        trace += (a[i][0] / na) * (b[i][0] / nb) + (a[i][1] / na) * (b[i][1] / nb) + (a[i][2] / na) * (b[i][2] / nb);
    }
    return std::acos(std::fmax(-1.0, std::fmin(1.0, (trace - 1.0) / 2.0))) * 57.29577951308232;
}

/// FollowTurn for an engine that keeps a transform as three axis rows of four
/// floats: turns `carried`'s rows in world space by the turn that took the view
/// from `clean` to `drawn`, scaled by `multiplier` about the same axis. The three
/// sets are rows in the same order and world frame; each carried row keeps its
/// length, and the fourth float of each is left alone, so a backend turns its own
/// transform in place. No turn, or a half turn, which has no one axis, leaves
/// `carried` alone.
inline void CarryTurn(float (*carried)[4], const float (*clean)[4], const float (*drawn)[4], double multiplier) {
    double c[3][3], d[3][3];
    for (int i = 0; i < 3; ++i) {
        const double nc = std::sqrt(double(clean[i][0]) * clean[i][0] + double(clean[i][1]) * clean[i][1] +
                                    double(clean[i][2]) * clean[i][2]);
        const double nd = std::sqrt(double(drawn[i][0]) * drawn[i][0] + double(drawn[i][1]) * drawn[i][1] +
                                    double(drawn[i][2]) * drawn[i][2]);
        for (int j = 0; j < 3; ++j) c[i][j] = clean[i][j] / nc, d[i][j] = drawn[i][j] / nd;
    }
    // Rows are row vectors, so drawn = clean * W and W = clean^T * drawn. r is W
    // as a column rotation (its transpose), which the axis and angle are read from.
    double r[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r[j][i] = c[0][i] * d[0][j] + c[1][i] * d[1][j] + c[2][i] * d[2][j];
    const double cosAngle = std::fmax(-1.0, std::fmin(1.0, (r[0][0] + r[1][1] + r[2][2] - 1.0) / 2.0));
    double axis[3] = {r[2][1] - r[1][2], r[0][2] - r[2][0], r[1][0] - r[0][1]};
    const double twoSin = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    if (twoSin < 1e-9) return;
    for (double& v : axis) v /= twoSin;
    const double angle = std::acos(cosAngle) * multiplier;
    const double k = std::cos(angle), s = std::sin(angle), t = 1.0 - k;
    const double x = axis[0], y = axis[1], z = axis[2];
    const double turn[3][3] = {{k + t * x * x, t * x * y - s * z, t * x * z + s * y},
                               {t * x * y + s * z, k + t * y * y, t * y * z - s * x},
                               {t * x * z - s * y, t * y * z + s * x, k + t * z * z}};
    for (int i = 0; i < 3; ++i) {
        const double v[3] = {carried[i][0], carried[i][1], carried[i][2]};
        for (int j = 0; j < 3; ++j)
            carried[i][j] = static_cast<float>(turn[j][0] * v[0] + turn[j][1] * v[1] + turn[j][2] * v[2]);
    }
}

}  // namespace cameraunlock::effects
