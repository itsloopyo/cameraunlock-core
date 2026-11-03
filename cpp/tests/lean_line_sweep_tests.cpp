// Tests for camera/lean_line_sweep.h against a small analytic world of boxes.
// The case the sweep exists for is TestEdgeBesideTheCentreLineBlocks: one line
// down the middle of the lean passes a door frame's edge and reports clear.

#include <cameraunlock/camera/lean_line_sweep.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

using cameraunlock::camera::LeanClamp;
using cameraunlock::camera::LeanClampSettings;
using cameraunlock::camera::LeanObstruction;
using cameraunlock::camera::LineHit;
using cameraunlock::camera::LineSweep;
using cameraunlock::camera::LineSweepQuery;
using cameraunlock::math::Vec3;

int g_failures = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

bool NearEqual(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

struct Box {
    Vec3 min, max;
};

// A half-space: everything on the side of the plane the normal points away from.
struct Wall {
    Vec3 normal;  // unit, facing the eye
    float offset; // plane: dot(normal, p) == offset
};

struct World {
    std::vector<Box> boxes;
    std::vector<Wall> walls;
    bool fail = false;
    // Casts from this many on cannot run.
    int failFrom = -1;
    // The cast gives no surface normal, as X-Ray's rain ray pick gives none.
    bool noNormals = false;
    int casts = 0;
};

// Slab test. A ray starting inside a box does not hit it, as with a physics
// engine's cast against a solid it starts in.
LineHit CastBox(const Box& b, const Vec3& s, const Vec3& d, float length) {
    LineHit out;
    out.queried = true;
    float tmin = 0.0f, tmax = length;
    int axis = -1;
    const float so[3] = {s.x, s.y, s.z}, dd[3] = {d.x, d.y, d.z};
    const float lo[3] = {b.min.x, b.min.y, b.min.z}, hi[3] = {b.max.x, b.max.y, b.max.z};
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(dd[i]) < 1e-9f) {
            if (so[i] < lo[i] || so[i] > hi[i]) return out;
            continue;
        }
        float t1 = (lo[i] - so[i]) / dd[i], t2 = (hi[i] - so[i]) / dd[i];
        if (t1 > t2) std::swap(t1, t2);
        if (t1 > tmin) {
            tmin = t1;
            axis = i;
        }
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return out;
    }
    if (axis < 0) return out;  // started inside
    out.hit = true;
    out.distance = tmin;
    out.normal = Vec3(axis == 0 ? 1.0f : 0.0f, axis == 1 ? 1.0f : 0.0f, axis == 2 ? 1.0f : 0.0f);
    return out;
}

LineHit CastWall(const Wall& w, const Vec3& s, const Vec3& d, float length) {
    LineHit out;
    out.queried = true;
    const float denom = Vec3::Dot(w.normal, d);
    if (denom >= 0.0f) return out;
    const float t = (w.offset - Vec3::Dot(w.normal, s)) / denom;
    if (t < 0.0f || t > length) return out;
    out.hit = true;
    out.distance = t;
    out.normal = w.normal;
    return out;
}

LineHit Cast(void* context, const Vec3& s, const Vec3& d, float length) {
    World& world = *static_cast<World*>(context);
    const bool fail = world.fail || (world.failFrom >= 0 && world.casts >= world.failFrom);
    ++world.casts;
    LineHit best;
    best.queried = !fail;
    if (fail) return best;
    const auto take = [&best](const LineHit& h) {
        if (h.hit && (!best.hit || h.distance < best.distance)) best = h;
    };
    for (const Box& b : world.boxes) take(CastBox(b, s, d, length));
    for (const Wall& w : world.walls) take(CastWall(w, s, d, length));
    if (world.noNormals) best.normal = Vec3::Zero();
    return best;
}

LineSweep MakeSweep(World& world, float radius = 0.1f) {
    LineSweep sweep;
    sweep.cast = &Cast;
    sweep.cast_context = &world;
    sweep.settings.radius = radius;
    return sweep;
}

// LeanClamp's call: the lean plus the skin.
LeanObstruction Ask(LineSweep& sweep, float lean, const Vec3& dir = Vec3(0.0f, 0.0f, 1.0f)) {
    return LineSweepQuery(&sweep, Vec3::Zero(), dir, lean + sweep.settings.radius);
}

void TestOpenSpaceIsClear() {
    World world;
    LineSweep sweep = MakeSweep(world);
    const LeanObstruction o = Ask(sweep, 0.3f);
    Check(o.queried && !o.blocked, "open space is a definite clear path");
    Check(world.casts == 1 + 2 * sweep.settings.ring_rays, "one centre ray, and a probe and a ray per ring slot");
}

void TestWallFacingTheLean() {
    World world;
    world.walls.push_back({Vec3(0.0f, 0.0f, -1.0f), -0.35f});  // plane z = 0.35
    LineSweep sweep = MakeSweep(world);
    const LeanObstruction o = Ask(sweep, 0.3f);
    Check(o.blocked, "a wall 0.35 ahead blocks a 0.3 lean with a 0.1 radius");
    Check(NearEqual(o.distance - sweep.settings.radius, 0.25f), "the centre stops a radius short of the wall");

    world.walls[0].offset = -1.0f;
    Check(!Ask(sweep, 0.3f).blocked, "a wall past the lean and the radius is clear");
}

void TestObliqueWallHoldsTheRadiusAlongItsNormal() {
    World world;
    // Plane through (0, 0, 0.5) with its normal 60 degrees off the lean.
    const Vec3 n(std::sin(1.04719755f), 0.0f, -std::cos(1.04719755f));
    world.walls.push_back({n, Vec3::Dot(n, Vec3(0.0f, 0.0f, 0.5f))});
    LineSweep sweep = MakeSweep(world);
    const LeanObstruction o = Ask(sweep, 0.45f);
    // Centre stops at 0.5 - 0.1 / cos(60) = 0.3, where it is 0.1 off the plane.
    Check(o.blocked && NearEqual(o.distance - 0.1f, 0.3f, 1e-3f), "an oblique wall stops the centre at radius / cos");
    const Vec3 at(0.0f, 0.0f, o.distance - 0.1f);
    Check(NearEqual(std::fabs(Vec3::Dot(n, at) - world.walls[0].offset), 0.1f, 1e-3f),
          "the stopped centre is a radius off the plane");
}

void TestEdgeBesideTheCentreLineBlocks() {
    World world;
    // A door frame: its face at z = 0.2, starting 5 cm to the side of the lean.
    world.boxes.push_back({Vec3(0.05f, -1.0f, 0.2f), Vec3(1.0f, 1.0f, 0.4f)});
    LineSweep sweep = MakeSweep(world);

    const LineHit centre = Cast(&world, Vec3::Zero(), Vec3(0.0f, 0.0f, 1.0f), 1.0f);
    Check(!centre.hit, "one line down the middle of the lean misses the frame");

    const LeanObstruction o = Ask(sweep, 0.3f);
    Check(o.blocked, "the ring catches the frame edge beside the centre line");
    Check(o.distance - 0.1f <= 0.2f + 1e-4f, "the eye stops before the frame's face");
}

void TestSlidingAlongAWallBesideTheEye() {
    World world;
    // A wall running along the lean, 4 cm to the side: closer than the radius.
    world.boxes.push_back({Vec3(0.04f, -1.0f, -1.0f), Vec3(1.0f, 1.0f, 2.0f)});
    LineSweep sweep = MakeSweep(world);
    const LeanObstruction o = Ask(sweep, 0.3f);
    Check(o.queried && !o.blocked, "a lean parallel to a wall already beside the eye is not blocked by it");
}

void TestFailedCastIsUnanswered() {
    World world;
    world.fail = true;
    LineSweep sweep = MakeSweep(world);
    Check(!Ask(sweep, 0.3f).queried, "a cast that cannot run leaves the query unanswered");
}

void TestClampStopsAtTheSweep() {
    World world;
    world.boxes.push_back({Vec3(0.05f, -1.0f, 0.2f), Vec3(1.0f, 1.0f, 0.4f)});
    LineSweep sweep = MakeSweep(world);
    LeanClamp clamp;
    LeanClampSettings settings;
    settings.skin = sweep.settings.radius;
    clamp.SetSettings(settings);
    const Vec3 out = clamp.Apply(Vec3::Zero(), Vec3(0.0f, 0.0f, 0.3f), 0.016f, &LineSweepQuery, &sweep);
    Check(clamp.InContact() && out.z <= 0.2f + 1e-4f && out.z > 0.0f,
          "LeanClamp with the sweep stops the lean short of an edge a line misses");
}

void TestLeanInAnyDirection() {
    World world;
    world.walls.push_back({Vec3(0.0f, -1.0f, 0.0f), -0.3f});  // ceiling at y = 0.3
    LineSweep sweep = MakeSweep(world);
    const LeanObstruction o = Ask(sweep, 0.25f, Vec3(0.0f, 1.0f, 0.0f));
    Check(o.blocked && NearEqual(o.distance - 0.1f, 0.2f), "a lean straight up stops a radius under the ceiling");
}

// A plane through `point` whose normal is `degrees` off the lean (+z), tilted
// about the y axis, facing the eye.
Wall TiltedWall(float degrees, const Vec3& point) {
    const float a = degrees * 0.01745329252f;
    const Vec3 n(std::sin(a), 0.0f, -std::cos(a));
    return {n, Vec3::Dot(n, point)};
}

void TestNoNormalWallFacingTheLean() {
    World world;
    world.noNormals = true;
    world.walls.push_back({Vec3(0.0f, 0.0f, -1.0f), -0.35f});
    LineSweep sweep = MakeSweep(world);
    const LeanObstruction o = Ask(sweep, 0.3f);
    Check(o.blocked && NearEqual(o.distance - 0.1f, 0.25f, 1e-3f),
          "with no normal from the cast, a wall facing the lean still stops the centre a radius short");
    Check(world.casts == 1 + 4 + 2 * sweep.settings.ring_rays, "four casts beside the centre ray measure the surface");
}

void TestNoNormalObliqueWall() {
    World world;
    world.noNormals = true;
    world.walls.push_back(TiltedWall(60.0f, Vec3(0.0f, 0.0f, 0.5f)));
    LineSweep sweep = MakeSweep(world);
    const LeanObstruction o = Ask(sweep, 0.45f);
    Check(o.blocked && NearEqual(o.distance - 0.1f, 0.3f, 1e-3f),
          "with no normal from the cast, an oblique wall stops the centre at radius / cos");
}

void TestNoNormalTiltedBothWays() {
    World world;
    world.noNormals = true;
    // Normal 40 degrees off the lean, out of the plane of either basis axis.
    const Vec3 n = Vec3(0.4f, 0.3f, -std::sqrt(1.0f - 0.25f));
    world.walls.push_back({n, Vec3::Dot(n, Vec3(0.0f, 0.0f, 0.6f))});
    LineSweep sweep = MakeSweep(world);
    const LeanObstruction o = Ask(sweep, 0.55f);
    const float c = std::fabs(n.z);
    Check(o.blocked && NearEqual(o.distance - 0.1f, 0.6f - 0.1f / c, 1e-3f),
          "the angle is measured whatever way the surface tilts across the lean");
}

void TestNoNormalSteepWallTakesTheWorstAngle() {
    World world;
    world.noNormals = true;
    // 75.7 degrees, just past the worst angle allowed (75.5), and not so steep
    // that the ring ray on its near side stops the sphere first.
    world.walls.push_back(TiltedWall(75.7f, Vec3(0.0f, 0.0f, 0.6f)));
    LineSweep sweep = MakeSweep(world);
    const LeanObstruction o = Ask(sweep, 0.55f);
    Check(o.blocked && NearEqual(o.distance - 0.1f, 0.6f - 0.1f / sweep.settings.min_cosine, 1e-3f),
          "a surface past the worst angle allowed is read at that angle");

    world.walls[0] = TiltedWall(80.0f, Vec3(0.0f, 0.0f, 0.6f));
    Vec3 u, v;
    cameraunlock::camera::detail::PerpendicularBasis(Vec3(0.0f, 0.0f, 1.0f), u, v);
    float c = 0.0f;
    const bool ran = cameraunlock::camera::detail::MeasuredCosine(sweep, Vec3::Zero(), Vec3(0.0f, 0.0f, 1.0f), u, v, 0.6f, c);
    Check(ran && c == sweep.settings.min_cosine, "an 80 degree surface measures as the worst angle allowed");
}

void TestNoNormalAtCentimetreScale() {
    World world;
    world.noNormals = true;
    world.walls.push_back(TiltedWall(30.0f, Vec3(0.0f, 0.0f, 300.0f)));
    LineSweep sweep = MakeSweep(world, 15.0f);
    const LeanObstruction o = Ask(sweep, 295.0f);
    Check(o.blocked && NearEqual(o.distance - 15.0f, 300.0f - 15.0f / std::cos(0.5235988f), 0.05f),
          "a 15 cm radius 3 m out holds the eye a radius off an oblique wall");
}

void TestWallWithANormalCastsNoMore() {
    World world;
    world.walls.push_back({Vec3(0.0f, 0.0f, -1.0f), -0.35f});
    LineSweep sweep = MakeSweep(world);
    Ask(sweep, 0.3f);
    Check(world.casts == 1 + 2 * sweep.settings.ring_rays, "a cast that gives a normal costs no casts beside the centre ray");
}

void TestNoNormalInsideCornerTakesTheWorstAngle() {
    World world;
    world.noNormals = true;
    // Two walls meeting in a vertical inside corner 0.6 ahead, each 45 degrees
    // off the lean. A flat reading of the corner would see a wall facing the
    // lean and hold the centre 0.1 off the corner, 0.07 off each wall.
    world.walls.push_back(TiltedWall(45.0f, Vec3(0.0f, 0.0f, 0.6f)));
    world.walls.push_back(TiltedWall(-45.0f, Vec3(0.0f, 0.0f, 0.6f)));
    LineSweep sweep = MakeSweep(world);
    const LeanObstruction o = Ask(sweep, 0.55f);
    Check(o.blocked && NearEqual(o.distance - 0.1f, 0.6f - 0.1f / sweep.settings.min_cosine, 1e-3f),
          "an inside corner is not read as one flat wall");
}

void TestNoNormalEdgeTakesTheWorstAngle() {
    World world;
    world.noNormals = true;
    // The centre ray meets a box's face 1 cm from its edge, so one ray beside
    // it passes the box.
    world.boxes.push_back({Vec3(-0.01f, -1.0f, 0.6f), Vec3(1.0f, 1.0f, 0.8f)});
    LineSweep sweep = MakeSweep(world);
    const LeanObstruction o = Ask(sweep, 0.55f);
    Check(o.blocked && NearEqual(o.distance - 0.1f, 0.6f - 0.1f / sweep.settings.min_cosine, 1e-3f),
          "a ray beside the centre that misses reads as the worst angle");
}

void TestNoNormalThinObjectBeforeAWallTakesTheWorstAngle() {
    World world;
    world.noNormals = true;
    // A slat 1 cm wide straight ahead with a wall 2 cm behind it. The rays
    // beside the centre pass the slat and meet the wall, which looks like a
    // bulge toward the eye but says nothing about the slat's own faces.
    world.boxes.push_back({Vec3(-0.005f, -1.0f, 0.6f), Vec3(0.005f, 1.0f, 1.0f)});
    world.walls.push_back({Vec3(0.0f, 0.0f, -1.0f), -0.62f});
    LineSweep sweep = MakeSweep(world);
    const LeanObstruction o = Ask(sweep, 0.55f);
    Check(o.blocked && NearEqual(o.distance - 0.1f, 0.6f - 0.1f / sweep.settings.min_cosine, 1e-3f),
          "a thin object before a wall is read at the worst angle allowed");
}

void TestNoNormalFailedMeasureIsUnanswered() {
    World world;
    world.noNormals = true;
    world.walls.push_back({Vec3(0.0f, 0.0f, -1.0f), -0.35f});
    LineSweep sweep = MakeSweep(world);
    for (int from = 1; from <= 4; ++from) {
        world.failFrom = from;
        world.casts = 0;
        Check(!Ask(sweep, 0.3f).queried, "a cast measuring the surface that cannot run leaves the query unanswered");
    }
}

}  // namespace

int RunLeanLineSweepTests() {
    std::cout << "\nLean line sweep tests:\n";
    g_failures = 0;

    TestOpenSpaceIsClear();
    TestWallFacingTheLean();
    TestObliqueWallHoldsTheRadiusAlongItsNormal();
    TestEdgeBesideTheCentreLineBlocks();
    TestSlidingAlongAWallBesideTheEye();
    TestFailedCastIsUnanswered();
    TestClampStopsAtTheSweep();
    TestLeanInAnyDirection();
    TestNoNormalWallFacingTheLean();
    TestNoNormalObliqueWall();
    TestNoNormalTiltedBothWays();
    TestNoNormalSteepWallTakesTheWorstAngle();
    TestNoNormalAtCentimetreScale();
    TestWallWithANormalCastsNoMore();
    TestNoNormalInsideCornerTakesTheWorstAngle();
    TestNoNormalEdgeTakesTheWorstAngle();
    TestNoNormalThinObjectBeforeAWallTakesTheWorstAngle();
    TestNoNormalFailedMeasureIsUnanswered();

    if (g_failures == 0) {
        std::cout << "Lean line sweep tests: all passed\n";
    } else {
        std::cout << "Lean line sweep tests: " << g_failures << " failure(s)\n";
    }
    return g_failures;
}
