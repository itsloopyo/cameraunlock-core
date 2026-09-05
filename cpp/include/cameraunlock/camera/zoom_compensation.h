#pragma once

#include <cmath>

// Keeps head tracking's effect on the picture the same size whatever the game
// does with its field of view.
//
// A game that zooms - iron sights, a scope, a cover peek, a vision aug, a
// cinematic - narrows its FOV, and a narrow FOV magnifies everything in the
// frame, head tracking included. The head still turns ten degrees and the
// camera still turns ten degrees; the picture just moves further, by the ratio
// between the two fields of view. Deus Ex: Human Revolution's 90 degree walking
// FOV against its 45 degree scope is 2.4x, and the player reads that as the
// mod's sensitivity changing under them the moment they aim.
//
// The correction is one number: scale the pose so its SCREEN displacement is
// what it would have been at the base FOV. It is exactly 1.0 when nothing is
// zoomed, and it shrinks with the zoom.
//
// This is not a sensitivity knob and not an ADS setting. It is an
// engine-boundary conversion in the same family as the axis signs and the unit
// scale: the tracker's pose is untouched, and what changes is the amount of
// engine rotation one tracker degree is worth once the engine's own projection
// has had its say. Nothing here is user-configurable.
//
// What scales and what does not:
//
//   - Yaw and pitch TRANSLATE the image across the frame, so both scale.
//   - A lean translates it too - a head offset d seen at depth D lands at
//     d / (2 * D * tan(fov/2)) of the frame - so position scales, linearly and
//     exactly.
//   - Roll ROTATES the image about the view axis. Ten degrees of head roll
//     rolls the picture ten degrees at every field of view there is, so roll is
//     left alone. Scaling it would flatten a head tilt the player is holding
//     and buy nothing.
namespace cameraunlock {
namespace camera {

/// The factor a translation - a lean - scales by, given the FOV being rendered
/// now and the game's un-zoomed one, both as tan(fov/2) in the same axis.
///
/// **The same axis is the whole of the difficulty.** An engine will hand you a
/// vertical FOV through the accessor its projection uses and a horizontal one
/// wherever its settings are authored, both floats, both radians, and pairing
/// them is not an error anything can catch: the ratio is merely off by a
/// constant, so the whole of normal play runs at a fixed fraction of the pose
/// and head tracking feels weak everywhere rather than wrong anywhere. Carry one
/// across with the aspect the engine's own projection divides x by -
/// `tan(h/2) = tan(v/2) * aspect` - and prove it by checking that this returns
/// 1.0 when the game is not zoomed.
///
/// Both must be finite and positive. They come out of game memory, so that is
/// the mod's boundary check to make, and a mod that cannot read the live FOV
/// applies no compensation rather than a guessed one.
inline float FovZoomFactor(float tan_half_fov, float tan_half_fov_base) {
    return tan_half_fov / tan_half_fov_base;
}

/// An angle in degrees, rescaled so it displaces the image by as much as the
/// original angle did at the base FOV.
///
/// The tangent round trip is what makes that exact rather than approximate: the
/// image displacement of an angle goes as tan(angle) / tan(fov/2), so holding
/// the ratio fixed means tan(out) = tan(in) * factor. For the small angles a
/// head reaches it is indistinguishable from multiplying, and it stays honest
/// at the large ones.
///
/// `factor` must be positive; `angle_deg` must be within +/-90, which every
/// pose a neck produces is.
inline float ScaleAngleForZoom(float angle_deg, float factor) {
    constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
    return std::atan(std::tan(angle_deg * kDegToRad) * factor) / kDegToRad;
}

}  // namespace camera
}  // namespace cameraunlock
