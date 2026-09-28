#pragma once

#include "cameraunlock/ads/lean_handover.h"
#include "cameraunlock/camera/lean_clamp.h"
#include "cameraunlock/math/vec3.h"

namespace cameraunlock::reframework {

// One frame of the lean, in world space.
struct RigLeanFrame {
    // Added to the eye the game computed this frame, in the render phase only.
    math::Vec3 camera;
    // What the rig should carry from the next LateUpdateBehavior on.
    math::Vec3 rigRequest;
    // The eye with no lean on either carrier, which the clamp swept from.
    math::Vec3 cleanEye;
};

// The positional lean of a REFramework mod whose rig moves a frame late.
//
// The rig (the transform the camera, the arms, the weapon and the round's start
// point hang off) is written at LateUpdateBehavior, before the game places the
// camera from it, while the lean is decided at BeginRendering, after. So the eye
// the game hands BeginRendering already carries whatever the rig was given a
// frame ago, and the rig's share decided now only reaches the eye next frame.
//
// Per frame: take the rig's applied offset back off the game's eye to recover
// the un-leaned eye, clamp the whole lean against the world from there (once,
// before the split, since the rig carries the muzzle), split it with
// LeanHandover, and hand the camera whatever the rig does not carry yet. The
// rendered eye is then the un-leaned eye plus the clamped lean on every frame,
// whichever carrier holds it and however far into the hand-over it is, and the
// camera's offset is exactly the gap between the eye and the round.
//
// Pure: no clock, no engine. nowMs comes from the caller, the query from the mod.
class RigLean {
public:
    // gameEye: the eye the game computed this frame, rigApplied included.
    // lean: the whole lean in world space, before the clamp.
    // rigApplied: what the rig carried this frame (zero if it was not written).
    // rigAvailable: false wherever the rig must stay where the game put it; the
    // lean then eases out on the camera while aiming, as with no rig at all.
    RigLeanFrame Update(const math::Vec3& gameEye, const math::Vec3& lean, const math::Vec3& rigApplied,
                        bool aiming, bool trueFreeLook, bool rigAvailable, float deltaTime,
                        unsigned long long nowMs, camera::LeanQueryFn query, void* queryContext) {
        RigLeanFrame frame;
        frame.cleanEye = gameEye - rigApplied;
        const math::Vec3 clamped = m_clamp.Apply(frame.cleanEye, lean, deltaTime, query, queryContext);
        const ads::LeanShares shares = m_handover.Update(clamped, aiming, trueFreeLook, rigAvailable, nowMs);
        frame.rigRequest = shares.rig;
        frame.camera = shares.camera + shares.rig - rigApplied;
        return frame;
    }

    // Every frame that applies no lean (position off, tracking off, a menu, a
    // camera cut) calls this instead of Update, and requests no rig offset. The
    // next Update starts at the hip with no allowance carried over. Returns true
    // when the rig was carrying some of the lean.
    bool Stop() {
        m_clamp.Reset();
        return m_handover.Stop();
    }

    camera::LeanClamp& Clamp() { return m_clamp; }
    const camera::LeanClamp& Clamp() const { return m_clamp; }

private:
    camera::LeanClamp m_clamp;
    ads::LeanHandover m_handover;
};

}  // namespace cameraunlock::reframework
