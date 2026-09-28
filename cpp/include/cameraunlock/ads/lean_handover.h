#pragma once

#include "cameraunlock/ads/ads_fade.h"
#include "cameraunlock/math/vec3.h"

#include <limits>

namespace cameraunlock::ads {

// The lean split between its two carriers (see LeanHandover).
struct LeanShares {
    // Moves the rendered view and nothing else.
    math::Vec3 camera;
    // Moves the transform the camera, the arms, the weapon and the round's start
    // point all hang off, so the sights stay on the eye and the round leaves from it.
    math::Vec3 rig;
};

// Hands the lateral part of a positional lean over from the camera to the rig as the
// sights come up, and back as they come down, riding AdsFade (see the
// shooter-ads-handling skill, "Carry the lateral lean on the rig").
//
// The part of the lean along the aim moves the eye along the sight line, which keeps
// it on the sights, so it stays on the camera in every mode: leaning in brings the
// sights closer. Only the part perpendicular to the aim is handed over.
//
// The lean goes in whole and already clamped against the world: the clamp runs
// once, before the split, because the rig carries the muzzle and the start point
// and an unclamped share would shoot through the wall. Both shares come back in
// the space the lean went in, so a mod converts the rig's share through world
// space when its rig's local frame is not the camera's; the two shares always sum
// to the lean, so the eye lands in the same place whichever carrier holds it.
//
// Only the camera's share opens a gap between the eye and the round, so it is the
// only share the aim hook and the reticle are handed.
//
// Pure: no clock, no logging, no game. nowMs comes from the caller.
class LeanHandover {
public:
    // The eye relief: how far forward of where the game puts the eye the lean may
    // take it while the sights are up, so the eye never passes the rear sight or a
    // scope's eyepiece. Measured once per game, in metres; a mod that never sets it
    // has no stop. It holds in both modes, since the weapon stays put in true free
    // look as well, and it eases in and out with the sights rather than stepping.
    void SetForwardStop(float metres) { m_forwardStop = metres; }

    // Once per rendered frame the lean is applied. `aimForward` is the unit aim
    // direction in the same space as `lean`, pointing the way the camera looks. `aiming` is the ADS state for this
    // frame, polled rather than latched. In true free look the camera keeps the
    // lean through the aim. `rigAvailable` is false wherever the rig must stay
    // where the game puts it (mounted in a vehicle seat, say): the lateral lean
    // then eases out on the sights instead, and the rig carries nothing.
    LeanShares Update(const math::Vec3& lean, const math::Vec3& aimForward, bool aiming,
                      bool trueFreeLook, bool rigAvailable, unsigned long long nowMs) {
        const float cameraShare = m_fade.Update(aiming && !trueFreeLook, nowMs);
        const float sightsUp = 1.0f - m_sightsFade.Update(aiming, nowMs);
        const float alongLength = math::Vec3::Dot(lean, aimForward);
        const math::Vec3 lateral = lean - aimForward * alongLength;
        float kept = alongLength;
        if (kept > m_forwardStop) kept += (m_forwardStop - kept) * sightsUp;
        const math::Vec3 along = aimForward * kept;
        LeanShares shares;
        shares.camera = along + lateral * cameraShare;
        shares.rig = rigAvailable ? lateral * (1.0f - cameraShare) : math::Vec3();
        m_rigEngaged = shares.rig.x != 0.0f || shares.rig.y != 0.0f || shares.rig.z != 0.0f;
        return shares;
    }

    // Every path that stops the lean - position off, tracking suspended, a menu,
    // a camera cut - calls this instead of Update. Returns true when the last
    // Update left a share on the rig, which the mod then puts back where the game
    // had it, because nothing in the game resets it. The next Update starts at
    // the hip.
    bool Stop() {
        const bool release = m_rigEngaged;
        m_rigEngaged = false;
        m_fade.Reset();
        m_sightsFade.Reset();
        return release;
    }

private:
    AdsFade m_fade;
    // Follows the sights alone, true free look or not, for the forward stop.
    AdsFade m_sightsFade;
    float m_forwardStop = std::numeric_limits<float>::infinity();
    bool m_rigEngaged = false;
};

}  // namespace cameraunlock::ads
