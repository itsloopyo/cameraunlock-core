// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <cmath>
#include <optional>

#include "cameraunlock/camera/head_bones.h"
#include "cameraunlock/unreal/ue_math.h"

// A held weapon the game draws as if through a FOV of its own, so that it looks
// the same at any FOV the world is drawn at. Colonial Marines (UE3) draws it
// through a projection that narrow (its MeshFOV); The Outer Worlds scales the
// weapon's vertices across the view's axis in its first-person material, by
// the drawn view's tan(FOV/2) over its own FOV's. Either way the weapon's
// picture is the world's projection scaled about the middle of the screen,
// which is right while the view looks down the aim and wrong once the head
// turns the view off it: the weapon then lands that many times as far from the
// middle as the aim does. Turning the weapon about the drawn eye by WeaponTurn
// puts the aim back under it. UE's frame (X forward, Y right, Z up).
namespace cameraunlock::camera::viewmodel {

/// How many times as wide, in half-angle tangent, the weapon's projection is as
/// the world's. `tanH` is the world's as drawn, `baseTanH` the world's at the
/// FOV drawn at longest (0 when not known yet, taken as now). A weapon whose FOV
/// narrows with the view's keeps its share of the angle.
inline double WeaponFovRatio(double weaponFovDeg, double tanH, double baseTanH, bool narrowsWithZoom) {
    constexpr double kRadPerDeg = 3.14159265358979323846 / 180.0;
    double half = weaponFovDeg * 0.5 * kRadPerDeg;
    if (narrowsWithZoom && baseTanH > 0) half *= std::atan(tanH) / std::atan(baseTanH);
    return std::tan(half) / tanH;
}

/// The world turn, about the drawn eye, that takes the clean aim to where a
/// projection `ratio` times as wide as the world's must see it for the aim to
/// land where the world's puts it: in the drawn view's frame its two lateral
/// parts scale by the ratio, by the shortest arc, so the weapon rolls no more
/// than it must. Nothing when the aim is not in front of the view.
inline std::optional<unreal::FQuat4d> WeaponTurn(const unreal::FQuat4d& drawn, const unreal::FQuat4d& clean, double ratio) {
    const unreal::FQuat4d toView = unreal::QuatInv(drawn);
    const unreal::FVector aim = unreal::QuatRotateVec(toView, unreal::QuatRotateVec(clean, {1.0, 0.0, 0.0}));
    if (!(aim.X > 0.05)) return std::nullopt;
    const unreal::FVector scaled{aim.X, aim.Y * ratio, aim.Z * ratio};
    const double length = std::sqrt(scaled.X * scaled.X + scaled.Y * scaled.Y + scaled.Z * scaled.Z);
    if (!(length > 1e-9)) return std::nullopt;
    const unreal::FVector want{scaled.X / length, scaled.Y / length, scaled.Z / length};
    const unreal::FQuat4d inView = unreal::QuatShortestArc(aim, want);
    return unreal::QuatNormalized(unreal::QuatMul(unreal::QuatMul(drawn, inView), toView));
}

/// The move, in the world, that gives a leaned view the weapon's parallax as the
/// world's projection draws it. The weapon stays at the clean eye while the view
/// is drawn from the leaned one, and its projection widens every offset from the
/// drawn axis by 1/ratio, the lean's with the rest, so it would slide that many
/// times as far as a weapon drawn like the world does (6 times at GameFov 105 on
/// 32:9 in The Outer Worlds). After `turn` about the drawn eye, the clean eye's
/// offset from the drawn eye is put at its own depth with its lateral parts
/// scaled by the ratio.
inline unreal::FVector WeaponLeanShift(const unreal::FQuat4d& drawn, const unreal::FQuat4d& turn,
                                       const unreal::FVector& cleanEye, const unreal::FVector& drawnEye, double ratio) {
    const unreal::FQuat4d toView = unreal::QuatInv(drawn);
    const unreal::FVector offset{cleanEye.X - drawnEye.X, cleanEye.Y - drawnEye.Y, cleanEye.Z - drawnEye.Z};
    const unreal::FVector before = unreal::QuatRotateVec(toView, offset);
    const unreal::FVector turned = unreal::QuatRotateVec(toView, unreal::QuatRotateVec(turn, offset));
    return unreal::QuatRotateVec(drawn, {before.X - turned.X, ratio * before.Y - turned.Y, ratio * before.Z - turned.Z});
}

/// A point carried by `turn` about `pivot`.
inline unreal::FVector TurnedAbout(const unreal::FVector& p, const unreal::FVector& pivot, const unreal::FQuat4d& turn) {
    const unreal::FVector d = unreal::QuatRotateVec(turn, {p.X - pivot.X, p.Y - pivot.Y, p.Z - pivot.Z});
    return {pivot.X + d.X, pivot.Y + d.Y, pivot.Z + d.Z};
}

/// A turn by `worldTurn` about `eye` and then a move by `shift`, given in world
/// space, as a transform of the component space of a mesh placed at `placed`
/// with uniform scale `scale`: a bone at p (unscaled component space) is drawn at
/// placed(scale * p), and after Compose(LocalTurn(...), p) at that point turned
/// about the eye and moved.
inline head_bones::Xf LocalTurn(const head_bones::Xf& placed, double scale, const unreal::FQuat4d& worldTurn,
                                const unreal::FVector& eye, const unreal::FVector& shift) {
    const unreal::FVector back = unreal::QuatRotateVec(worldTurn, {-eye.X, -eye.Y, -eye.Z});
    const head_bones::Xf turn{worldTurn, {eye.X + back.X + shift.X, eye.Y + back.Y + shift.Y, eye.Z + back.Z + shift.Z}};
    head_bones::Xf local = head_bones::Compose(head_bones::Inverse(placed), head_bones::Compose(turn, placed));
    local.t = {local.t.X / scale, local.t.Y / scale, local.t.Z / scale};
    return local;
}

}  // namespace cameraunlock::camera::viewmodel
