// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include "cameraunlock/ads/lean_handover.h"
#include "cameraunlock/math/vec3.h"

namespace cameraunlock::ads {

/// The lean split between the world camera and a first-person skeleton root,
/// which the camera bone, the arms, the weapon and its projectile node hang off.
/// The game updates that skeleton early in the frame and places the camera
/// later, so the rig share decided at this frame's camera is written at the
/// next frame's skeleton update, and the camera makes up the difference: the
/// eye is always the clean eye plus the whole lean.
/// From fallout-new-vegas-headtracking src/rig_lean.h, unchanged.
///
/// reframework::RigLean solves the same one-frame lag for a REFramework mod,
/// where the game's eye already carries the rig's share and the lean is also
/// clamped against the world. This one takes a lean already clamped and is told
/// at the skeleton update whether the camera sits on the skeleton.
class DeferredRigLean {
public:
    using V = math::Vec3;

    /// At the skeleton update: the world offset to add to the root this frame.
    /// Nothing in third person, where the camera does not sit on the skeleton.
    V TakeRig(bool firstPerson) {
        m_applied = firstPerson ? m_next : V();
        m_next = V();
        return m_applied;
    }

    /// The rig share inside the eye the camera reads this frame.
    const V& Applied() const { return m_applied; }

    /// At the camera, with the whole lean clamped from the clean eye and the
    /// clean camera's unit forward axis, both in world axes. Returns the offset
    /// to add to the camera's eye: only the lean across the aim goes to the rig,
    /// and none in either free look mode.
    V Split(const V& lean, const V& aimForward, bool aiming, bool trueFreeLook, bool rigAvailable,
            unsigned long long nowMs) {
        m_next = m_handover.Update(lean, aimForward, aiming, trueFreeLook, rigAvailable, nowMs).rig;
        return lean - m_applied;
    }

    /// End of the displayed frame: the game rewrites the root before its next
    /// update, so nothing carries over.
    void EndFrame() { m_applied = V(); }

    void Stop() {
        m_handover.Stop();
        m_next = V();
        m_applied = V();
    }

private:
    LeanHandover m_handover;
    V m_next;
    V m_applied;
};

}  // namespace cameraunlock::ads
