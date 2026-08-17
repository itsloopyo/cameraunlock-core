#pragma once

#include <cameraunlock/math/rotation_utils.h>
#include <cmath>

namespace cameraunlock::rendering {

// Parameters for crosshair projection calculation
struct CrosshairProjectionParams {
    // Screen dimensions
    float screenWidth = 1920.0f;
    float screenHeight = 1080.0f;

    // Camera field of view in degrees (horizontal)
    float fovDegrees = 75.0f;

    // Head tracking offsets in degrees
    // Positive yaw = looking right, positive pitch = looking up
    float yawOffset = 0.0f;
    float pitchOffset = 0.0f;
    float rollOffset = 0.0f;

    // Base camera pitch from game input (gamepad/mouse) in radians
    // This is the pitch before head tracking is applied
    float gameCameraPitch = 0.0f;
};

// Result of crosshair projection
struct ScreenPosition {
    float x = 0.0f;
    float y = 0.0f;
    bool valid = false;
};

// Constants
constexpr float kDegToRad = 0.0174532925f;  // pi / 180

// Project crosshair position based on head tracking and camera state.
// This computes where the "body aim" direction appears on screen when
// the camera has been rotated by head tracking.
//
// The rotation model uses camera-local axes:
//   Yaw around camera up (0,1,0), Pitch around camera right (0,0,-1).
//   Only forward is rotated; up is re-derived.
// This matches ApplyHeadTrackingRotation in rotation_math.h.
//
// Coordinate system: X=forward, Y=up, Z=left (DL2)
inline ScreenPosition ProjectCrosshair(const CrosshairProjectionParams& params) {
    ScreenPosition result;
    result.valid = false;

    // FOV for perspective projection
    float aspectRatio = params.screenWidth / params.screenHeight;
    float hFovRad = params.fovDegrees * kDegToRad;
    float tanHalfHFov = std::tan(hFovRad / 2.0f);
    float tanHalfVFov = tanHalfHFov / aspectRatio;

    // Match camera hook sign conventions exactly:
    //   yaw = -processedYaw * DEG_TO_RAD
    //   pitch = processedPitch * DEG_TO_RAD
    //   roll = processedRoll * DEG_TO_RAD
    float yawRad = -params.yawOffset * kDegToRad;
    float pitchRad = params.pitchOffset * kDegToRad;
    float rollRad = params.rollOffset * kDegToRad;

    // Camera space before head tracking: fwd=(1,0,0), up=(0,1,0), right=(0,0,-1)
    // Construct forward from spherical coordinates — matches ApplyHeadTrackingRotation.
    // This ensures yaw and pitch are independent (no arc artifacts).
    float cosY = std::cos(yawRad), sinY = std::sin(yawRad);
    float cosP = std::cos(pitchRad), sinP = std::sin(pitchRad);

    // fwd = cosP*cosY*forward + cosP*sinY*right - sinP*up
    // In camera space: forward=(1,0,0), up=(0,1,0), right=(0,0,-1)
    float fwd[3] = {
        cosP * cosY,        // forward component
        -sinP,              // vertical component
        -cosP * sinY        // horizontal component (right = -Z in DL2)
    };

    // Re-derive up: project origUp=(0,1,0) perpendicular to new fwd
    float origUp[3] = {0.0f, 1.0f, 0.0f};
    float dot = cameraunlock::math::Dot3(fwd, origUp);
    float newUp[3] = {origUp[0] - fwd[0] * dot, origUp[1] - fwd[1] * dot, origUp[2] - fwd[2] * dot};
    if (cameraunlock::math::Normalize3(newUp) < 0.0001f) {
        result.x = params.screenWidth / 2.0f;
        result.y = params.screenHeight / 2.0f;
        result.valid = true;
        return result;
    }

    // Roll: rotate up around new fwd
    if (std::fabs(rollRad) >= 0.001f) {
        float tmp[3];
        cameraunlock::math::RotateAroundAxis(newUp, fwd, rollRad, tmp);
        newUp[0] = tmp[0]; newUp[1] = tmp[1]; newUp[2] = tmp[2];
    }

    // Left axis of new camera frame
    float newLeft[3];
    cameraunlock::math::Cross3(fwd, newUp, newLeft);

    // Body aim = (1,0,0) projected into new camera frame.
    // Since body = (1,0,0), dot products simplify to [0] components.
    float bDepth = fwd[0];
    float bUp    = newUp[0];
    float bLeft  = newLeft[0];

    // Body aim at or behind the camera plane. Clamping bDepth to +0.01f here flipped
    // the sign of the perspective divide for negative depth, so past ~90 degrees of head
    // turn the marker did not disappear - it teleported to the opposite edge and pinned
    // there, pointing the player away from where their shots actually go.
    //
    // Reported as invalid, matching ProjectAimQuatHorPlus, which applies the identical
    // depth test and documents inFront=false as "hide the reticle instead of drawing
    // it". Behind-camera is a reachable half-space, not a measure-zero singularity like
    // the degenerate paths above, so a valid centre would claim the body aim is dead
    // ahead while it is 100 degrees away. Centre coordinates are still written, so a
    // consumer that ignores the flag lands on centre rather than on stale values.
    if (bDepth < 0.01f) {
        result.x = params.screenWidth / 2.0f;
        result.y = params.screenHeight / 2.0f;
        result.valid = false;
        return result;
    }

    // NaN check
    if (bDepth != bDepth || bUp != bUp || bLeft != bLeft) {
        result.x = params.screenWidth / 2.0f;
        result.y = params.screenHeight / 2.0f;
        result.valid = true;
        return result;
    }

    // Perspective projection: body aim direction in screen space.
    float normalizedX = bLeft / (bDepth * tanHalfHFov);
    float normalizedY = -bUp / (bDepth * tanHalfVFov);

    // Convert to screen pixels (Y inverted for screen coordinates)
    float cx = (params.screenWidth / 2.0f) + normalizedX * (params.screenWidth / 2.0f);
    float cy = (params.screenHeight / 2.0f) - normalizedY * (params.screenHeight / 2.0f);

    // Final NaN check
    if (cx != cx || cy != cy) {
        cx = params.screenWidth / 2.0f;
        cy = params.screenHeight / 2.0f;
    }

    result.x = cx;
    result.y = cy;
    result.valid = true;

    return result;
}

// Clamp screen position to visible area with margin
inline void ClampToScreen(ScreenPosition& pos, float screenWidth, float screenHeight, float margin = 10.0f) {
    if (pos.x < margin) pos.x = margin;
    if (pos.x > screenWidth - margin) pos.x = screenWidth - margin;
    if (pos.y < margin) pos.y = margin;
    if (pos.y > screenHeight - margin) pos.y = screenHeight - margin;
}

} // namespace cameraunlock::rendering
