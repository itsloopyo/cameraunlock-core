// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <array>
#include <cmath>

namespace cameraunlock::unreal {

// A render scale under this is no scale to take back out of a measurement.
inline constexpr double kNoRenderScale = 1e-3;

// How many viewport pixels one unit of a widget's render translation moves it,
// across and down, from where its painted geometry lays the local points
// `probe` units along its own X (alongX) and along its own Y (alongY), each
// less where it lays its origin.
//
// Slate lays a widget's render transform as its scale, then its shear, then its
// turn, then its translation (FWidgetTransform's FSlateRenderTransform), so the
// translation is in the axes of the layout above the widget, not turned with
// it. The measured columns are that layout's scale across and down times the
// widget's own turned scale, which is known, so the layout's is taken back out
// of whichever column carries more of each axis.
//
// The widget has no shear of its own, which this does not take out, and a
// render scale of at least kNoRenderScale on each axis; the caller refuses
// any other.
inline std::array<double, 2> TranslationScale(const std::array<double, 2>& alongX, const std::array<double, 2>& alongY,
                                              double probe, double scaleX, double scaleY, double angleDegrees) {
    const double turn = angleDegrees * 3.14159265358979323846 / 180.0;
    const double c = std::cos(turn), s = std::sin(turn);
    // FQuat2D's turn of (x, y) is (x cos - y sin, x sin + y cos), laid after the scale.
    const double m00 = c * scaleX, m01 = -s * scaleY, m10 = s * scaleX, m11 = c * scaleY;
    // The diagonal unless the turn has moved more of the axis off it, so with an
    // even scale a turn of exactly 45 degrees reads the diagonal whichever way
    // it turns.
    const double across = std::fabs(m01) > std::fabs(m00) ? alongY[0] / probe / m01 : alongX[0] / probe / m00;
    const double down = std::fabs(m10) > std::fabs(m11) ? alongX[1] / probe / m10 : alongY[1] / probe / m11;
    return std::array<double, 2>{across, down};
}

}  // namespace cameraunlock::unreal
