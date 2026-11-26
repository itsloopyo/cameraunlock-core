// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

#include "cameraunlock/unreal/ue_math.h"

// Turning a character's neck and head bones by the tracked head, the same in
// every engine. The frame is UE's (X forward, Y right, Z up); a backend
// converts its bones into it and back.
namespace cameraunlock::camera::head_bones {

// A bone's component-space rotation and translation. Its scale is the game's
// and is never written.
struct Xf {
    unreal::FQuat4d q{0, 0, 0, 1};
    unreal::FVector t{0, 0, 0};
};

// `a` after `b`.
inline Xf Compose(const Xf& a, const Xf& b) {
    const unreal::FVector r = unreal::QuatRotateVec(a.q, b.t);
    return {unreal::QuatMul(a.q, b.q), {r.X + a.t.X, r.Y + a.t.Y, r.Z + a.t.Z}};
}

inline Xf Inverse(const Xf& a) {
    const unreal::FQuat4d inv = unreal::QuatInv(a.q);
    const unreal::FVector t = unreal::QuatRotateVec(inv, a.t);
    return {inv, {-t.X, -t.Y, -t.Z}};
}

// The neck takes this share of the turn and the head the rest.
constexpr double kNeckShare = 0.4;

// As far as a neck goes, in degrees. A tracker turned further leaves the head
// at the limit (the fleet's Requiem limits).
constexpr double kYawLimit = 80, kUpLimit = 50, kDownLimit = 45, kRollLimit = 35;

// The world turn of a head facing `facingYaw` (degrees) by the tracked yaw,
// pitch and roll, each scaled by `multiplier` and held to a neck's limits. The
// pose is laid on the facing, so a head pitched down stays pitched down
// whichever way the character faces. Roll goes in as -roll, the sign a view
// composed with QuatFromEulerDeg(pitch, yaw, -roll) uses, so the head tilts
// with the view.
inline unreal::FQuat4d WorldTurn(double facingYaw, double yaw, double pitch, double roll, double multiplier) {
    yaw = std::clamp(yaw * multiplier, -kYawLimit, kYawLimit);
    pitch = std::clamp(pitch * multiplier, -kDownLimit, kUpLimit);
    roll = std::clamp(roll * multiplier, -kRollLimit, kRollLimit);
    const unreal::FQuat4d facing = unreal::QuatFromEulerDeg(0, facingYaw, 0);
    return unreal::QuatNormalized(unreal::QuatMul(
        facing, unreal::QuatMul(unreal::QuatFromEulerDeg(pitch, yaw, -roll), unreal::QuatInv(facing))));
}

// The same turn seen from the mesh, whose component-to-world rotation is `mesh`.
inline unreal::FQuat4d InComponent(const unreal::FQuat4d& worldTurn, const unreal::FQuat4d& mesh) {
    return unreal::QuatNormalized(unreal::QuatMul(unreal::QuatInv(mesh), unreal::QuatMul(worldTurn, mesh)));
}

// Turns `neck` by `neckShare` of `turn` (component space) and `head` by the
// rest, each about its own joint, and carries every bone below them along.
// `parent` holds each bone's parent index, -1 for the root, a parent always
// before its children. Returns which bones changed; `pose` is changed in place.
inline std::vector<bool> TurnNeckAndHead(std::vector<Xf>& pose, const std::vector<int>& parent, int neck, int head,
                                         const unreal::FQuat4d& turn, double neckShare) {
    const std::size_t n = pose.size();
    std::vector<bool> moved(n, false);
    std::vector<Xf> move(n);
    const unreal::FQuat4d neckTurn = unreal::QuatScaleTurn(turn, neckShare);
    const unreal::FQuat4d headTurn = unreal::QuatNormalized(unreal::QuatMul(turn, unreal::QuatInv(neckTurn)));
    const auto turnAbout = [](const unreal::FQuat4d& by, const Xf& before) {
        // About the bone's own joint: the rotation is changed, the place kept.
        const Xf after{unreal::QuatNormalized(unreal::QuatMul(by, before.q)), before.t};
        return Compose(after, Inverse(before));
    };
    for (std::size_t i = 0; i < n; ++i) {
        const int p = parent[i];
        const bool below = p >= 0 && moved[static_cast<std::size_t>(p)];
        if (static_cast<int>(i) == neck) {
            move[i] = turnAbout(neckTurn, pose[i]);
        } else if (static_cast<int>(i) == head) {
            // The neck has already carried the head; it turns from there.
            const Xf carried = below ? Compose(move[static_cast<std::size_t>(p)], pose[i]) : pose[i];
            const Xf own = turnAbout(headTurn, carried);
            move[i] = below ? Compose(own, move[static_cast<std::size_t>(p)]) : own;
        } else if (below) {
            move[i] = move[static_cast<std::size_t>(p)];
        } else {
            continue;
        }
        moved[i] = true;
        pose[i] = Compose(move[i], pose[i]);
    }
    return moved;
}

}  // namespace cameraunlock::camera::head_bones
