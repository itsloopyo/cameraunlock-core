#pragma once

#include <cmath>

namespace cameraunlock::rendering {

constexpr float kGuiDegToRad = 0.0174532925f;  // pi / 180

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

// Convert perspective-projection diagonal terms to pixel focal lengths.
// P[0][0] = 1/tan(hFovX/2) and P[1][1] = 1/tan(hFovY/2) in NDC; multiplying by
// the half-canvas dimensions yields pixel focal lengths with no horizontal-vs-
// vertical FOV convention guessing. Returns false when the terms fall outside
// the plausible perspective range (orthographic/degenerate projection).
inline bool FocalLengthsFromProjection(float p00, float p11,
                                       float halfCanvasW, float halfCanvasH,
                                       float& fx, float& fy) {
    if (p00 < 0.1f || p00 > 20.f || p11 < 0.1f || p11 > 20.f) return false;
    if (!(halfCanvasW > 0.f) || !(halfCanvasH > 0.f)) return false;
    fx = p00 * halfCanvasW;
    fy = p11 * halfCanvasH;
    return true;
}

// Fallback when the projection matrix is unavailable: derive pixel focal
// lengths from a vertical FOV in degrees at the canvas aspect ratio. Returns
// false for an implausible FOV.
inline bool FocalLengthsFromVerticalFov(float fovDegY,
                                        float halfCanvasW, float halfCanvasH,
                                        float& fx, float& fy) {
    if (fovDegY < 10.f || fovDegY > 170.f) return false;
    if (!(halfCanvasW > 0.f) || !(halfCanvasH > 0.f)) return false;
    float aspect = halfCanvasW / halfCanvasH;
    float tanHalfFovY = std::tan(fovDegY * kGuiDegToRad * 0.5f);
    fx = halfCanvasW / (tanHalfFovY * aspect);
    fy = halfCanvasH / tanHalfFovY;
    return true;
}

}  // namespace cameraunlock::rendering
