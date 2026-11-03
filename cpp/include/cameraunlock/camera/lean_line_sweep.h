#pragma once

#include <cmath>

#include "cameraunlock/camera/lean_clamp.h"
#include "cameraunlock/math/vec3.h"

// A swept sphere for LeanClamp, built out of line casts.
//
// Some engines expose only a line cast to a mod (HPL2 and HPL3 among them). One
// line along the lean guards the centre of the eye's path and nothing around
// it: the eye can pass a door frame's edge, a shelf's lip or a table corner a
// few millimetres off the line, and the near plane then culls the corner and the
// player looks through it. A sphere swept along the lean is what the eye
// actually needs kept clear, and this approximates it:
//
//  - one ray down the centre, which gives the exact answer for any flat surface
//    the lean runs into, at any angle;
//  - where the cast gives no surface normal, four rays beside the centre ray,
//    whose hits give the surface's slope and so its angle to the lean;
//  - a ring of rays around it, parallel to the lean, on the sphere's equator,
//    which catches edges and corners the centre ray passes beside;
//  - a short sideways probe before each ring ray, so a ring ray never starts
//    inside a surface that is already closer to the eye than the radius.
//
// The radius is the clamp's skin. LeanClamp holds the eye `skin` back from the
// distance it is handed, so the sweep hands back the travel the sphere allows
// PLUS the radius, and the two must be the same number: set
// LineSweepSettings::radius from LeanClampSettings::skin.
namespace cameraunlock {
namespace camera {

/// What one line cast found.
struct LineHit {
    /// False when the cast could not run at all.
    bool queried = false;
    /// True when the cast met something within its length.
    bool hit = false;
    /// From the cast's start along its direction, in the caller's units.
    float distance = 0.0f;
    /// The surface normal at the hit. Either facing is fine; only its angle to
    /// the cast is used. A zero normal is an unknown one: the sweep measures
    /// the surface's angle with four more casts, and reads it as the worst
    /// angle allowed where those do not find one flat surface.
    math::Vec3 normal{0.0f, 0.0f, 0.0f};
};

/// The engine's line cast. `direction` is a unit vector, `length` how far to
/// look. It must skip the player's own body: the eye starts inside it.
using LineCastFn = LineHit (*)(void* context, const math::Vec3& start, const math::Vec3& direction,
                               float length);

struct LineSweepSettings {
    /// The sphere's radius, in the caller's units. Must equal the clamp's skin.
    float radius = 0.10f;

    /// Rays in the ring around the centre ray. More close smaller gaps between
    /// them, and each costs two casts (the probe and the ray).
    int ring_rays = 8;

    /// The floor on the cosine between the lean and a surface's normal. A flat
    /// surface met at an angle has to be traced further along the lean to hold
    /// the eye `radius` off it, by radius / cos, which has no bound at grazing
    /// incidence. 0.25 is 75 degrees off the normal, past which the eye slides
    /// along the surface rather than into it.
    float min_cosine = 0.25f;
};

/// The line cast and the settings, passed as the `context` of LineSweepQuery.
struct LineSweep {
    LineCastFn cast = nullptr;
    void* cast_context = nullptr;
    LineSweepSettings settings;
};

namespace detail {

// Two unit vectors perpendicular to `d` and to each other.
inline void PerpendicularBasis(const math::Vec3& d, math::Vec3& u, math::Vec3& v) {
    // Crossed with the world axis least aligned with d, so the cross product
    // never degenerates.
    const float ax = std::fabs(d.x), ay = std::fabs(d.y), az = std::fabs(d.z);
    const math::Vec3 helper = (ax <= ay && ax <= az) ? math::Vec3(1.0f, 0.0f, 0.0f)
                            : (ay <= az)             ? math::Vec3(0.0f, 1.0f, 0.0f)
                                                     : math::Vec3(0.0f, 0.0f, 1.0f);
    const math::Vec3 a(d.y * helper.z - d.z * helper.y, d.z * helper.x - d.x * helper.z,
                       d.x * helper.y - d.y * helper.x);
    u = a * (1.0f / a.Magnitude());
    v = math::Vec3(d.y * u.z - d.z * u.y, d.z * u.x - d.x * u.z, d.x * u.y - d.y * u.x);
}

// The cosine between the lean and the surface the centre ray met `distance`
// out, for a cast that gave no normal. Four rays parallel to the centre ray,
// half a radius off it along u and v, sample the surface around the hit. On a
// flat surface the hit distance changes by tan(angle) per unit across it, so
// the two slopes give the angle. Each ray looks only as far as a surface at
// the worst angle allowed would put it, so a surface that steep, or an edge
// beside the hit, leaves a ray with no hit. A ray with no hit, or a pair whose
// hits bend away from a line through the centre hit (an inside corner, where a
// flat reading would let the sphere into both walls), answers min_cosine. The
// bend allowed is a slope change of 0.1 across each pair, about 6 degrees met
// head-on and less at an angle, so a shallow crease or a gently hollow surface
// can still read as flat and let the eye a few percent of the radius closer
// than the radius. False when a cast could not run.
inline bool MeasuredCosine(const LineSweep& sweep, const math::Vec3& start, const math::Vec3& direction,
                           const math::Vec3& u, const math::Vec3& v, float distance, float& cosine) {
    const float min_cos = sweep.settings.min_cosine;
    const float spacing = sweep.settings.radius * 0.5f;
    const float steepest = std::sqrt(1.0f - min_cos * min_cos) / min_cos;
    const float length = distance + spacing * steepest * 1.01f;
    const float bend = spacing * 0.1f;
    const math::Vec3 axes[2] = {u, v};
    float slope[2];
    cosine = min_cos;
    for (int a = 0; a < 2; ++a) {
        const LineHit plus = sweep.cast(sweep.cast_context, start + axes[a] * spacing, direction, length);
        if (!plus.queried) return false;
        const LineHit minus = sweep.cast(sweep.cast_context, start - axes[a] * spacing, direction, length);
        if (!minus.queried) return false;
        if (!plus.hit || !minus.hit || std::fabs(plus.distance + minus.distance - 2.0f * distance) > bend) return true;
        slope[a] = (plus.distance - minus.distance) / (2.0f * spacing);
    }
    const float c = 1.0f / std::sqrt(1.0f + slope[0] * slope[0] + slope[1] * slope[1]);
    if (c > min_cos) cosine = c;
    return true;
}

}  // namespace detail

/// A LeanQueryFn. `context` is a LineSweep. `start` is the clean eye,
/// `direction` the unit lean direction and `max_distance` what LeanClamp asks
/// for: the lean plus the skin.
///
/// Any cast that cannot run makes the whole query unanswered (queried=false),
/// which the clamp reports rather than treating as clear.
inline LeanObstruction LineSweepQuery(void* context, const math::Vec3& start, const math::Vec3& direction,
                                      float max_distance) {
    const LineSweep& sweep = *static_cast<const LineSweep*>(context);
    const float r = sweep.settings.radius;
    const float min_cos = sweep.settings.min_cosine;
    const float lean = max_distance - r;

    LeanObstruction out;
    // The furthest the sphere's centre may travel, starting from the whole lean.
    float travel = lean;

    math::Vec3 u, v;
    detail::PerpendicularBasis(direction, u, v);

    // Centre ray. For a flat surface at distance d whose normal is at cos c to
    // the lean, the sphere touches it once its centre has gone d - r / c.
    const LineHit centre = sweep.cast(sweep.cast_context, start, direction, lean + r / min_cos);
    if (!centre.queried) return out;
    if (centre.hit) {
        float c;
        if (centre.normal.x == 0.0f && centre.normal.y == 0.0f && centre.normal.z == 0.0f) {
            if (!detail::MeasuredCosine(sweep, start, direction, u, v, centre.distance, c)) return out;
        } else {
            c = std::fabs(math::Vec3::Dot(direction, centre.normal));
            if (!(c > min_cos)) c = min_cos;
        }
        const float t = centre.distance - r / c;
        if (t < travel) travel = t;
    }
    const int n = sweep.settings.ring_rays;
    for (int k = 0; k < n; ++k) {
        const float angle = 6.28318530717958647692f * static_cast<float>(k) / static_cast<float>(n);
        const math::Vec3 side = u * std::cos(angle) + v * std::sin(angle);

        // Where on the ring this ray starts. A surface closer beside the eye
        // than the radius pulls it in, halfway to that surface, so it starts in
        // open space and runs parallel to the lean.
        float offset = r;
        const LineHit probe = sweep.cast(sweep.cast_context, start, side, r);
        if (!probe.queried) return out;
        if (probe.hit && probe.distance < r) offset = probe.distance * 0.5f;

        // A point at `offset` off the centre line is on the sphere's surface
        // sqrt(r^2 - offset^2) ahead of the centre, so the sphere touches what
        // this ray meets at d once its centre has gone d minus that.
        const float ahead = std::sqrt(r * r - offset * offset);
        const LineHit ray = sweep.cast(sweep.cast_context, start + side * offset, direction, lean + ahead);
        if (!ray.queried) return out;
        if (ray.hit) {
            const float t = ray.distance - ahead;
            if (t < travel) travel = t;
        }
    }

    out.queried = true;
    if (travel >= lean) return out;
    out.blocked = true;
    // LeanClamp subtracts its skin (== r) from this.
    out.distance = (travel > 0.0f ? travel : 0.0f) + r;
    return out;
}

}  // namespace camera
}  // namespace cameraunlock
