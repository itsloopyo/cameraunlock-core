// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <cmath>

namespace cameraunlock::camera {

/// The axis an engine's FOV is measured on.
enum class FovAxis { Vertical, Horizontal };

namespace fov {

constexpr double kHalfDegToRad = 3.14159265358979323846 / 360.0;

/// `verticalDeg` on `axis` for a picture `aspect` wide (width over height): the
/// aspect the engine's own projection divides x by. Horizontal is
/// tan(h/2) = tan(v/2) * aspect.
inline double FromVertical(double verticalDeg, FovAxis axis, double aspect) {
    if (axis == FovAxis::Vertical) return verticalDeg;
    return std::atan(std::tan(verticalDeg * kHalfDegToRad) * aspect) / kHalfDegToRad;
}

/// The way back: `deg` on `axis` as a vertical FOV.
inline double ToVertical(double deg, FovAxis axis, double aspect) {
    if (axis == FovAxis::Vertical) return deg;
    return std::atan(std::tan(deg * kHalfDegToRad) / aspect) / kHalfDegToRad;
}

/// The FOV to draw at, on the engine's axis, for the game's own `gameDeg`
/// against its un-zoomed `baseDeg` (both on the engine's axis) and a player's
/// `targetVerticalDeg`. The tangent ratio of game to base is kept, so a scope
/// that halves the tangent of the game's setting halves the tangent of the
/// player's. 0 for the target, a game or base value that is not a positive
/// finite angle under 180, or an aspect that is not positive and finite, gives
/// `gameDeg` back unchanged, and the caller logs why. The result stays at or
/// under 179 degrees.
inline double Rescale(double gameDeg, double baseDeg, double targetVerticalDeg, FovAxis axis, double aspect) {
    const auto angle = [](double d) { return std::isfinite(d) && d > 0 && d < 180; };
    if (!(targetVerticalDeg > 0) || !angle(gameDeg) || !angle(baseDeg) || !(aspect > 0) || !std::isfinite(aspect))
        return gameDeg;
    const double target = FromVertical(targetVerticalDeg, axis, aspect);
    const double k = std::tan(target * kHalfDegToRad) / std::tan(baseDeg * kHalfDegToRad);
    const double out = std::atan(std::tan(gameDeg * kHalfDegToRad) * k) / kHalfDegToRad;
    return out < 179.0 ? out : 179.0;
}

}  // namespace fov
}  // namespace cameraunlock::camera
