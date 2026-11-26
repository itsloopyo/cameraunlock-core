#pragma once

#include <cmath>

namespace cameraunlock::rendering {

// Quaternion-relative aim-to-screen projection with Hor+ (MaintainYFOV) FOV
// scaling. Projects the clean-aim direction into the head-tracked view to find
// where the body-forward reticle should be drawn.
//
// Input is the relative rotation qrel = trackedView^-1 * cleanView as a unit
// quaternion, using UE camera-local axes (X = forward, Y = right, Z = up).
// This is the projection to use for UE titles and any engine that hands you a
// relative quaternion rather than per-axis angles:
//   - quaternion composition keeps combined yaw+pitch+roll exact (no
//     per-axis tangent drift, see the AGENTS.md reticle-compensation notes);
//   - Hor+ scaling holds the vertical FOV constant across aspect ratios and
//     expands horizontal with width, which is how UE5 (and most modern
//     engines) actually scale. The naive Vert- model (tanV = tanH / aspect)
//     coincides at 16:9 but over-rotates the reticle ~2x horizontally at 32:9.
//
// fovHorizontalAt16x9 is the engine's aspect-independent FOV scalar in degrees
// (UE FMinimalViewInfo.FOV), treated as the horizontal FOV at the 16:9
// reference aspect.

struct AimQuatProjection {
    // NDC in [-1, 1] (clamped), and the same position in backbuffer pixels.
    float ndcX = 0.0f;
    float ndcY = 0.0f;
    float screenX = 0.0f;
    float screenY = 0.0f;
    // False when the aim direction is behind the tracked view (extreme head
    // turn) - hide the reticle instead of drawing it.
    bool inFront = false;
};

inline AimQuatProjection ProjectAimQuatHorPlus(
    double qx, double qy, double qz, double qw,
    float screenWidth, float screenHeight,
    float fovHorizontalAt16x9,
    float referenceAspect = 16.0f / 9.0f) {
    AimQuatProjection result;

    // Clean-aim direction (UE forward +X) in the tracked camera frame
    // = qrel * (1,0,0): first column of qrel's rotation matrix.
    const double depth = 1.0 - 2.0 * (qy*qy + qz*qz);
    const double right = 2.0 * (qx*qy + qw*qz);
    const double up    = 2.0 * (qx*qz - qw*qy);
    if (depth <= 0.01) {
        return result;  // aim behind tracked view
    }

    // Hor+ FOV: derive the constant vertical FOV from the 16:9-reference
    // horizontal value, then re-expand horizontal by the live aspect.
    const float aspect = screenWidth / screenHeight;
    const float tanV = std::tan(fovHorizontalAt16x9 * 0.5f * 0.01745329252f) / referenceAspect;
    const float tanH = tanV * aspect;
    float ndcX = static_cast<float>(right / depth) / tanH;
    float ndcY = static_cast<float>(up    / depth) / tanV;

    // Clamp at the viewport edge. Without this, body-forward approaching 90deg
    // off-axis sends right/depth into the thousands (depth -> 0) and the
    // reticle shoots far off-screen. Capping NDC to |1| pins it to the screen
    // edge instead of accelerating away.
    if (ndcX >  1.0f) ndcX =  1.0f;
    if (ndcX < -1.0f) ndcX = -1.0f;
    if (ndcY >  1.0f) ndcY =  1.0f;
    if (ndcY < -1.0f) ndcY = -1.0f;

    result.ndcX = ndcX;
    result.ndcY = ndcY;
    result.screenX = screenWidth  * 0.5f + ndcX * (screenWidth  * 0.5f);
    result.screenY = screenHeight * 0.5f - ndcY * (screenHeight * 0.5f);
    result.inFront = true;
    return result;
}

}  // namespace cameraunlock::rendering
