#pragma once

#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>

#include "cameraunlock/unreal/ue_math.h"

namespace cameraunlock::rendering {

// Where the gun is pointing, in the picture the head is looking at.
//
// The game draws its crosshair at a fixed screen position, so it marks where
// shots land only while the rendered view IS the aim. Once the head turns the
// view away from the aim, the two are different directions and the crosshair is
// a lie - which is the whole reason this exists.
//
// This is the projection to reach for first. Of the three core carries it makes
// the fewest assumptions about the engine: it never names a field of view model,
// an aspect convention or an axis order, only the basis the frame was actually
// drawn with. Pick one of the others when the mod does not have that basis:
//
//   aim_quat_projection.h   the engine hands over a RELATIVE QUATERNION rather
//                           than a basis, and scales FOV Hor+ (UE5 and most
//                           modern engines).
//   crosshair_projection.h  the mod has per-axis yaw/pitch/roll and a single
//                           horizontal FOV, and composes camera-locally.
//
// The projection takes a unit vector FROM THE EYE THE FRAME IS DRAWN FROM TO THE
// POINT THE SHOT WILL LAND ON, and the basis the frame is actually rendered
// with, and asks where the first lands in the second. With the head centred that
// vector is just the clean camera's forward; with a positional lean it swings by
// the parallax, which is the whole reason it is a vector to a point and not a
// direction.
//
// It is deliberately basis-to-basis rather than a formula in yaw / pitch / roll:
// the camera hook composes the head delta in one of two modes and hands the
// vectors it actually wrote into the view matrix straight to here, so there is no
// second derivation of the composition to disagree with the first. A per-axis
// tangent formula would agree with it on single-axis poses and drift on combined
// ones, which is precisely the bug that survives testing.
//
// `tanX` / `tanY` are the DRAWN half-field tangents (tan(fovX/2), tan(fovY/2)),
// read back out of the render view after the projection has been narrowed - not
// the wider one the engine culls against.
//
// Returns false when the aim points behind the rendered view (the head has
// turned past 90 degrees off the gun), where there is no screen position to
// draw at. NDC is x right, y up, both -1..1 across the frame.
inline bool ProjectAimToNdc(const float aim[3],
                            const float fwd[3], const float right[3], const float up[3],
                            float tanX, float tanY, float& ndcX, float& ndcY) {
    const float z = aim[0] * fwd[0] + aim[1] * fwd[1] + aim[2] * fwd[2];
    // Not just "behind the camera": as z goes to zero the projection goes to
    // infinity, and a reticle at 1e30 is a NaN waiting to be handed to a vertex
    // buffer. A tenth of the depth range is far outside any frame anyway.
    if (!(z > 0.1f) || !(tanX > 0.0f) || !(tanY > 0.0f)) return false;

    const float x = aim[0] * right[0] + aim[1] * right[1] + aim[2] * right[2];
    const float y = aim[0] * up[0] + aim[1] * up[1] + aim[2] * up[2];
    ndcX = (x / z) / tanX;
    ndcY = (y / z) / tanY;
    return std::isfinite(ndcX) && std::isfinite(ndcY);
}


// Double-precision projection and unprojection for mods that hold their views
// as an eye, a basis and the drawn half-field tangents. Engines differ on when
// a point counts as in front of the view, so every call that projects takes
// that rule from the caller; nothing here picks one. Like ProjectAimToNdc they
// take the basis the frame was drawn with and name no axis order; only
// ViewBasisFromRotator, for callers holding an Unreal rotator, does.

// x right, y up, -1..1 across the frame.
struct NdcPoint {
    double x = 0, y = 0;
};

// Unit forward, right and up of a view, in world axes.
struct ViewBasis {
    unreal::FVector forward, right, up;
};

// The basis of a view turned by an Unreal rotator: forward is the rotator's
// +X, right its +Y, up its +Z.
inline ViewBasis ViewBasisFromRotator(const unreal::FRotator& rotation) {
    const unreal::FQuat4d q = unreal::QuatFromEulerDeg(rotation.Pitch, rotation.Yaw, rotation.Roll);
    return {unreal::QuatRotateVec(q, {1, 0, 0}), unreal::QuatRotateVec(q, {0, 1, 0}), unreal::QuatRotateVec(q, {0, 0, 1})};
}

// A view as a frame is drawn from it: the eye, the basis and the half-field
// tangents tan(fovX/2) and tan(fovY/2).
struct ViewFrame {
    unreal::FVector eye;
    ViewBasis basis;
    double tanX = 0, tanY = 0;
};

// When an offset from the eye projects: its depth along forward must exceed
// minDepth (in the offset's units) and exceed minCosine times the offset's
// length. {0, 0} is anything past the eye's plane. A floor above zero keeps the
// perspective divide from running off to infinity near the side planes.
struct InFrontRule {
    double minDepth = 0;
    double minCosine = 0;
};

namespace detail {
inline double Dot(const unreal::FVector& a, const unreal::FVector& b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z; }
inline bool InFront(const unreal::FVector& offset, double depth, const InFrontRule& rule) {
    if (!(depth > rule.minDepth)) return false;
    if (rule.minCosine == 0) return true;
    return depth > rule.minCosine * std::sqrt(Dot(offset, offset));
}
}  // namespace detail

// Where `offset`, a point less the eye or a direction (a point at infinity),
// lands in a view with basis `view` and half-field tangents `tanX`, `tanY`.
// Empty when `front` says it is not in front, a tangent is not positive, or the
// result is not finite.
inline std::optional<NdcPoint> ProjectOffsetToNdc(const unreal::FVector& offset, const ViewBasis& view, double tanX, double tanY,
                                                  const InFrontRule& front) {
    const double depth = detail::Dot(offset, view.forward);
    if (!(tanX > 0) || !(tanY > 0) || !detail::InFront(offset, depth, front)) return std::nullopt;
    const NdcPoint out{detail::Dot(offset, view.right) / depth / tanX, detail::Dot(offset, view.up) / depth / tanY};
    if (!std::isfinite(out.x) || !std::isfinite(out.y)) return std::nullopt;
    return out;
}

// Where the world point `point` lands in the view drawn from `eye`.
inline std::optional<NdcPoint> ProjectPointToNdc(const unreal::FVector& point, const unreal::FVector& eye, const ViewBasis& view,
                                                 double tanX, double tanY, const InFrontRule& front) {
    return ProjectOffsetToNdc({point.X - eye.X, point.Y - eye.Y, point.Z - eye.Z}, view, tanX, tanY, front);
}

// As above, for a view turned by an Unreal rotator.
inline std::optional<NdcPoint> ProjectPointToNdc(const unreal::FVector& point, const unreal::FVector& eye,
                                                 const unreal::FRotator& rotation, double tanX, double tanY, const InFrontRule& front) {
    return ProjectPointToNdc(point, eye, ViewBasisFromRotator(rotation), tanX, tanY, front);
}

// How a point that does not project is sent to the picture's edge: along its
// right and up components as they are (ViewUnits), or along them divided by
// tanX and tanY, the way it would cross a picture that is not square (Ndc).
enum class EdgeDirection { ViewUnits, Ndc };

struct EdgePinRule {
    // When the point projects normally.
    InFrontRule front;
    // Which way a point that does not project goes to the edge.
    EdgeDirection behind = EdgeDirection::Ndc;
    // Whether a point in front but outside the frame is also pulled in to the
    // edge, rather than left where it projects.
    bool pinInFront = false;
    // A point that does not project gives nothing, rather than an edge, when
    // the larger of its two components is not above this, in the units
    // `behind` names. Nearly straight behind, the side it lies on is rounding
    // noise. 0 gives nothing only for exactly straight behind.
    double behindMinEdge = 0;
};

// ProjectOffsetToNdc for a mark that must always show which way the shot went:
// a point beside or behind the view is put on the frame's edge (the larger of
// |x| and |y| is 1) on its side, instead of failing. Empty only for an offset
// that does not project and is straight behind or at the eye (no component
// above rule.behindMinEdge), a result that is not finite, or a tangent that is
// not positive.
inline std::optional<NdcPoint> ProjectOffsetToNdcEdgePinned(const unreal::FVector& offset, const ViewBasis& view, double tanX,
                                                            double tanY, const EdgePinRule& rule) {
    if (!(tanX > 0) || !(tanY > 0)) return std::nullopt;
    double x = detail::Dot(offset, view.right), y = detail::Dot(offset, view.up);
    const double depth = detail::Dot(offset, view.forward);
    const bool projects = detail::InFront(offset, depth, rule.front);
    if (projects) {
        x = x / depth / tanX;
        y = y / depth / tanY;
        if (!std::isfinite(x) || !std::isfinite(y)) return std::nullopt;
    } else if (rule.behind == EdgeDirection::Ndc) {
        x /= tanX;
        y /= tanY;
    }
    const double edge = std::fmax(std::fabs(x), std::fabs(y));
    if (!projects && !(edge > rule.behindMinEdge)) return std::nullopt;
    if (!projects || (rule.pinInFront && edge > 1.0)) {
        if (!(edge > 0)) return std::nullopt;
        x /= edge;
        y /= edge;
    }
    if (!std::isfinite(x) || !std::isfinite(y)) return std::nullopt;
    return NdcPoint{x, y};
}

// The direction through `ndc` of a view with basis `view` and half-field
// tangents `tanX`, `tanY`: forward plus the point's offset across the plane one
// unit ahead. Not normalised; its forward component is 1.
inline unreal::FVector UnprojectNdc(const NdcPoint& ndc, const ViewBasis& view, double tanX, double tanY) {
    const double a = ndc.x * tanX, b = ndc.y * tanY;
    return {view.forward.X + a * view.right.X + b * view.up.X, view.forward.Y + a * view.right.Y + b * view.up.Y,
            view.forward.Z + a * view.right.Z + b * view.up.Z};
}

// The world point `distance` from `eye` along the ray through `ndc`. Throws
// for a distance that is not finite: a point at infinity is the ray's
// direction, UnprojectNdc.
inline unreal::FVector PointThroughNdc(const NdcPoint& ndc, const unreal::FVector& eye, const ViewBasis& view, double tanX,
                                       double tanY, double distance) {
    if (!std::isfinite(distance)) throw std::invalid_argument("PointThroughNdc: the distance is not finite");
    const unreal::FVector d = UnprojectNdc(ndc, view, tanX, tanY);
    const double s = distance / std::sqrt(detail::Dot(d, d));
    return {eye.X + d.X * s, eye.Y + d.Y * s, eye.Z + d.Z * s};
}

// Where the aim through the point `at` of the clean picture lands in the drawn
// one. With a distance, the aim is the point that far down the clean ray, so a
// lean moves it by the parallax; without, it is the ray's direction, a point at
// infinity that only a turn moves. A distance of +infinity is the same as
// none. `front` is tested on the offset from the drawn eye, or without a
// distance on the unnormalised ray UnprojectNdc gives.
inline std::optional<NdcPoint> ReprojectNdc(const NdcPoint& at, const ViewFrame& clean, const ViewFrame& drawn,
                                            std::optional<double> distance, const InFrontRule& front) {
    unreal::FVector v = UnprojectNdc(at, clean.basis, clean.tanX, clean.tanY);
    if (distance && *distance != std::numeric_limits<double>::infinity()) {
        const unreal::FVector p = PointThroughNdc(at, clean.eye, clean.basis, clean.tanX, clean.tanY, *distance);
        v = {p.X - drawn.eye.X, p.Y - drawn.eye.Y, p.Z - drawn.eye.Z};
    }
    return ProjectOffsetToNdc(v, drawn.basis, drawn.tanX, drawn.tanY, front);
}

}  // namespace cameraunlock::rendering
