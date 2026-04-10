#pragma once

#include <cmath>

namespace cameraunlock::rendering {

constexpr float kGuiDegToRad = 0.0174532925f;  // pi / 180

// Input parameters for GUI marker world-anchor compensation.
// The game's GUI system projects world-anchored markers (interaction
// prompts, objective icons, etc.) through its own camera state which is
// NOT affected by our head-tracking hooks. To keep the marker pinned to
// the world object in the head-rotated view, we compute a screen-space
// delta to apply to the marker's local position (via set_Position on
// the marker's child TransformObject).
struct GuiMarkerInput {
    // Head rotation from the tracking processor (degrees).
    float yawDeg   = 0.f;
    float pitchDeg = 0.f;
    float rollDeg  = 0.f;

    // Head position offset from the tracking processor (meters,
    // in camera-local coordinates). Same values used by the 3D hook.
    float posX = 0.f;
    float posY = 0.f;
    float posZ = 0.f;

    // Camera vertical field of view (degrees). Read live from
    // via.Camera.get_FOV each frame. RE Engine returns this as a double
    // despite the TDB declaring it as Single — use ReadFovFromInvokeRet
    // to handle both representations transparently.
    float fovDegY = 46.f;

    // Native screen position of the marker (pixels relative to screen
    // center). If the engine exposes the marker's global position, use
    // it here for off-center correction. If not (common in RE Engine
    // where get_GlobalPosition returns 0), pass (0, 0) — the
    // compensation still works but has a minor U-shape artifact at
    // extreme yaw for off-center targets.
    float gxNative = 0.f;
    float gyNative = 0.f;

    // Screen dimensions in pixels.
    float screenW = 1920.f;
    float screenH = 1080.f;

    // Assumed target depth in meters. Used to convert the position
    // offset (meters) into a screen-space angular shift. 2m is a
    // reasonable default for typical interactable distances. When the
    // position offset is zero, this value has no effect (the depth
    // cancels out of the rotation-only projection).
    float assumedDepth = 2.f;
};

// Output: the pixel delta to write to the marker's TransformObject
// position (via set_Position on the child Control).
struct GuiMarkerResult {
    float deltaX = 0.f;
    float deltaY = 0.f;
    bool valid = false;
};

// Compute the screen-space delta that keeps a world-anchored GUI marker
// pinned to its target in the head-rotated + head-translated view.
//
// Math summary:
//   1. Convert the marker's native screen position to a camera-local 3D
//      direction at the assumed target depth.
//   2. Apply the camera-local position offset (from 6DOF tracking).
//   3. Build the head-rotation matrix R = Ry(y) * Rx(p) * Rz(-r) and
//      apply R^T to transform the target from the clean camera frame to
//      the head-rotated camera frame. Roll is negated to match the
//      empirically verified sign convention.
//   4. Reproject through the perspective FOV to get the new screen pos.
//   5. Return (new - native) as the delta to write.
//
// Sign conventions (empirically verified on RE9, expected to hold across
// RE Engine titles):
//   - Yaw:  direct (no negation)
//   - Pitch: direct (no negation)
//   - Roll:  negated (-roll)
//   - Position X: Xc += posX  (camera right is +X)
//   - Position Y: Yc -= posY
//   - Position Z: Zc -= posZ
inline GuiMarkerResult ComputeGuiMarkerCompensation(const GuiMarkerInput& in) {
    GuiMarkerResult out;

    if (in.fovDegY < 10.f || in.fovDegY > 170.f) return out;

    const float aspect = in.screenW / in.screenH;
    const float halfW  = in.screenW * 0.5f;
    const float halfH  = in.screenH * 0.5f;
    const float fovY   = in.fovDegY * kGuiDegToRad;
    const float tanHalfFovY = std::tan(fovY * 0.5f);
    const float tanHalfFovX = tanHalfFovY * aspect;
    const float Fx = halfW / tanHalfFovX;  // focal length (pixels)
    const float Fy = halfH / tanHalfFovY;

    // Camera-space position of the world anchor (meters).
    const float depth = in.assumedDepth;
    float Xc = (in.gxNative / Fx) * depth;
    float Yc = (in.gyNative / Fy) * depth;
    float Zc = depth;

    // Apply 6DOF position offset (camera-local translation).
    Xc += in.posX;
    Yc -= in.posY;
    Zc -= in.posZ;

    // Build rotation matrix R = Ry(y) * Rx(p) * Rz(-r).
    // Roll is negated per empirical verification (F1/F2/F3 toggle test).
    const float yawRad   =  in.yawDeg   * kGuiDegToRad;
    const float pitchRad =  in.pitchDeg * kGuiDegToRad;
    const float rollRad  = -in.rollDeg  * kGuiDegToRad;

    const float sy = std::sin(yawRad),   cy = std::cos(yawRad);
    const float sp = std::sin(pitchRad), cp = std::cos(pitchRad);
    const float sr = std::sin(rollRad),  cr = std::cos(rollRad);

    // R = Ry * Rx * Rz (expanded)
    const float r11 = cy*cr + sy*sp*sr;
    const float r12 = -cy*sr + sy*sp*cr;
    const float r13 = sy*cp;
    const float r21 = cp*sr;
    const float r22 = cp*cr;
    const float r23 = -sp;
    const float r31 = -sy*cr + cy*sp*sr;
    const float r32 = sy*sr + cy*sp*cr;
    const float r33 = cy*cp;

    // Apply R^T (transpose) to (Xc, Yc, Zc). The 3D hook rotates the
    // camera by R, so in the rotated camera frame a world anchor at
    // original camera-space P is at R^T * P.
    const float xR = r11*Xc + r21*Yc + r31*Zc;
    const float yR = r12*Xc + r22*Yc + r32*Zc;
    const float zR = r13*Xc + r23*Yc + r33*Zc;

    if (zR < 1e-4f) return out;  // behind camera

    const float newX = (xR / zR) * Fx;
    const float newY = (yR / zR) * Fy;

    // Clamp to reasonable screen range.
    constexpr float kMax = 1600.f;
    out.deltaX = newX - in.gxNative;
    out.deltaY = newY - in.gyNative;
    if (out.deltaX >  kMax) out.deltaX =  kMax;
    if (out.deltaX < -kMax) out.deltaX = -kMax;
    if (out.deltaY >  kMax) out.deltaY =  kMax;
    if (out.deltaY < -kMax) out.deltaY = -kMax;
    out.valid = true;
    return out;
}

// Helper: read FOV from REFramework's InvokeRet union.
// RE Engine's via.Camera.get_FOV is declared as Single in the TDB but
// the native ABI stores the return value as a double. Reading r.f gives
// 0 for typical FOV magnitudes because the low 32 bits of the double
// are zero; r.d gives the correct value.
//
// This helper tries both interpretations and returns whichever falls in
// a sane range [10, 170] degrees. Returns 0 if neither is valid.
inline float ReadFovFromInvokeRet(float asFloat, double asDouble) {
    if (asFloat >= 10.f && asFloat <= 170.f) return asFloat;
    float fromDouble = static_cast<float>(asDouble);
    if (fromDouble >= 10.f && fromDouble <= 170.f) return fromDouble;
    return 0.f;
}

}  // namespace cameraunlock::rendering
