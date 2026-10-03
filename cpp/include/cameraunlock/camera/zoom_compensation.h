#pragma once

#include <cameraunlock/math/vec3.h>

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
//   - A lean ACROSS the view translates it too - a head offset d seen at depth
//     D lands at d / (2 * D * tan(fov/2)) of the frame - so the part of the
//     lean perpendicular to the view axis scales, linearly and exactly.
//   - A lean ALONG the view moves nothing across the frame: it brings the scene
//     closer. It is left alone, because scaled it would cut short how far the
//     player can lean in, and a lean in through a 4x scope would keep a quarter
//     of its travel. ScaleLeanForZoom makes the split.
//   - Roll ROTATES the image about the view axis. Ten degrees of head roll
//     rolls the picture ten degrees at every field of view there is, so roll is
//     left alone. Scaling it would flatten a head tilt the player is holding
//     and buy nothing.
namespace cameraunlock {
namespace camera {

/// The factor a translation across the view - a sideways or vertical lean -
/// scales by, given the FOV being rendered
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

/// A lean with the part perpendicular to the view axis scaled by `factor` and
/// the part along it left as it is.
///
/// `view_axis` is the direction the camera looks along, unit length, in the
/// frame `lean` is in: (0, 0, 1) for a lean in the camera's own axes where z is
/// the view axis, the camera's forward vector for a lean in world space. Its
/// sign makes no difference.
inline math::Vec3 ScaleLeanForZoom(const math::Vec3& lean, const math::Vec3& view_axis, float factor) {
    const math::Vec3 along = view_axis * math::Vec3::Dot(lean, view_axis);
    return along + (lean - along) * factor;
}

}  // namespace camera
}  // namespace cameraunlock
