// Characterization tests for camera/lean_clamp.h. The clamp decides how far a
// 6DOF lean is allowed to travel before it puts the eye inside the level, and
// the two things most likely to be "tidied" later are exactly the two that make
// it work: the asymmetry between tightening and releasing, and the difference
// between a query that answered "clear" and one that could not answer at all.

#include <cameraunlock/camera/lean_clamp.h>

#include <cmath>
#include <iostream>

namespace {

using cameraunlock::camera::LeanClamp;
using cameraunlock::camera::LeanClampSettings;
using cameraunlock::camera::LeanObstruction;
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

bool NearEqual(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

// The queries below are driven through this so a test can say what the world
// looks like without each one needing its own function.
struct World {
    bool queried = true;
    bool blocked = false;
    float distance = 0.0f;
    int calls = 0;
};

LeanObstruction Query(void* context, const Vec3&, const Vec3&, float) {
    World* world = static_cast<World*>(context);
    ++world->calls;
    LeanObstruction out;
    out.queried = world->queried;
    out.blocked = world->blocked;
    out.distance = world->distance;
    return out;
}

LeanClamp MakeClamp(float skin, float release_smoothing = 0.9f) {
    LeanClamp clamp;
    LeanClampSettings settings;
    settings.skin = skin;
    settings.release_smoothing = release_smoothing;
    clamp.SetSettings(settings);
    return clamp;
}

// A null query is how every mod ships until its clamp has been tested in game,
// so this is the path most users are on and it must not touch the offset.
void TestNullQueryPassesThrough() {
    LeanClamp clamp = MakeClamp(0.10f);
    const Vec3 desired(0.3f, 0.0f, 0.0f);
    const Vec3 out = clamp.Apply(Vec3(1.0f, 2.0f, 3.0f), desired, 0.016f, nullptr, nullptr);

    Check(NearEqual(out.x, 0.3f) && NearEqual(out.y, 0.0f) && NearEqual(out.z, 0.0f),
          "null query passes the lean through unchanged");
    Check(!clamp.InContact(), "null query reports no contact");
    Check(!clamp.LastQueryFailed(), "null query is not a failed query");
}

void TestClearPathPassesThrough() {
    LeanClamp clamp = MakeClamp(0.10f);
    World world;
    world.blocked = false;
    const Vec3 desired(0.0f, 0.0f, 0.4f);
    const Vec3 out = clamp.Apply(Vec3::Zero(), desired, 0.016f, &Query, &world);

    Check(world.calls == 1, "a lean runs the query exactly once");
    Check(NearEqual(out.z, 0.4f), "a clear path leaves the lean at full magnitude");
    Check(!clamp.InContact(), "a clear path reports no contact");
}

void TestBlockedLeanStopsShortBySkin() {
    LeanClamp clamp = MakeClamp(0.10f);
    World world;
    world.blocked = true;
    world.distance = 0.25f;

    const Vec3 out = clamp.Apply(Vec3::Zero(), Vec3(0.4f, 0.0f, 0.0f), 0.016f, &Query, &world);

    Check(NearEqual(out.x, 0.15f), "a blocked lean stops at the hit distance minus the skin");
    Check(clamp.InContact(), "a blocked lean reports contact");
}

void TestDirectionIsPreserved() {
    LeanClamp clamp = MakeClamp(0.10f);
    World world;
    world.blocked = true;
    world.distance = 0.30f;

    // 0.3 / 0.4 / 0.0, magnitude 0.5. Allowed magnitude is 0.30 - 0.10 = 0.20,
    // so the components scale by 0.4.
    const Vec3 out = clamp.Apply(Vec3::Zero(), Vec3(0.3f, 0.4f, 0.0f), 0.016f, &Query, &world);

    Check(NearEqual(out.Magnitude(), 0.20f), "the clamped lean has the allowed magnitude");
    Check(NearEqual(out.x, 0.12f) && NearEqual(out.y, 0.16f),
          "the clamped lean keeps the direction it was asked for");
}

// A surface inside the skin, and the eye already inside geometry, are the same
// case: there is no room, so there is no lean.
void TestNoRoomCollapsesToZero() {
    LeanClamp clamp = MakeClamp(0.10f);
    World world;
    world.blocked = true;
    world.distance = 0.04f;

    const Vec3 tight = clamp.Apply(Vec3::Zero(), Vec3(0.3f, 0.0f, 0.0f), 0.016f, &Query, &world);
    Check(NearEqual(tight.Magnitude(), 0.0f), "a surface inside the skin allows no lean");

    clamp.Reset();
    world.distance = 0.0f;
    const Vec3 inside = clamp.Apply(Vec3::Zero(), Vec3(0.3f, 0.0f, 0.0f), 0.016f, &Query, &world);
    Check(NearEqual(inside.Magnitude(), 0.0f), "an eye already inside geometry allows no lean");
}

// The clamp must never ease INTO a tighter allowance: the eye would sit inside
// the wall for the duration of the ease, which is the bug the whole thing
// exists to prevent.
void TestTighteningIsInstant() {
    LeanClamp clamp = MakeClamp(0.10f);
    World world;

    world.blocked = false;
    clamp.Apply(Vec3::Zero(), Vec3(0.4f, 0.0f, 0.0f), 0.016f, &Query, &world);

    world.blocked = true;
    world.distance = 0.15f;
    const Vec3 out = clamp.Apply(Vec3::Zero(), Vec3(0.4f, 0.0f, 0.0f), 0.016f, &Query, &world);

    Check(NearEqual(out.x, 0.05f), "a wall appearing tightens the allowance in one frame");
}

// Releasing is the other half of the asymmetry: instant here would pop the view
// the moment an obstruction clears.
void TestReleaseIsDamped() {
    LeanClamp clamp = MakeClamp(0.10f);
    World world;

    world.blocked = true;
    world.distance = 0.15f;
    clamp.Apply(Vec3::Zero(), Vec3(0.4f, 0.0f, 0.0f), 0.016f, &Query, &world);

    world.blocked = false;
    const Vec3 first = clamp.Apply(Vec3::Zero(), Vec3(0.4f, 0.0f, 0.0f), 0.016f, &Query, &world);

    Check(first.x > 0.05f, "the allowance opens once the obstruction clears");
    Check(first.x < 0.4f, "the allowance does not jump straight back to full");
    Check(clamp.InContact(), "the view is still held short while the release runs");

    for (int frame = 0; frame < 240; ++frame)
        clamp.Apply(Vec3::Zero(), Vec3(0.4f, 0.0f, 0.0f), 0.016f, &Query, &world);
    const Vec3 settled = clamp.Apply(Vec3::Zero(), Vec3(0.4f, 0.0f, 0.0f), 0.016f, &Query, &world);

    Check(NearEqual(settled.x, 0.4f, 1e-3f), "the release converges on the full lean");
    Check(!clamp.InContact(), "contact clears once the release has converged");
}

// A query that could not run is not a query that found nothing. Absorbing the
// difference would leave a clamp that has silently stopped clamping looking
// exactly like one that never engaged.
void TestFailedQueryIsReportedNotAbsorbed() {
    LeanClamp clamp = MakeClamp(0.10f);
    World world;
    world.queried = false;

    const Vec3 out = clamp.Apply(Vec3::Zero(), Vec3(0.4f, 0.0f, 0.0f), 0.016f, &Query, &world);

    Check(NearEqual(out.x, 0.4f), "a failed query passes the lean through unclamped");
    Check(clamp.LastQueryFailed(), "a failed query is reported to the caller");
    Check(!clamp.InContact(), "a failed query is not reported as contact");

    world.queried = true;
    world.blocked = false;
    clamp.Apply(Vec3::Zero(), Vec3(0.4f, 0.0f, 0.0f), 0.016f, &Query, &world);
    Check(!clamp.LastQueryFailed(), "the failure flag clears once the query works again");
}

// Standing up straight must not leave the last wall's allowance behind, or the
// next lean is rationed through the release ease by a wall the player has
// already walked away from.
void TestNeutralPoseClearsTheAllowance() {
    LeanClamp clamp = MakeClamp(0.10f);
    World world;

    world.blocked = true;
    world.distance = 0.12f;
    clamp.Apply(Vec3::Zero(), Vec3(0.4f, 0.0f, 0.0f), 0.016f, &Query, &world);

    const int calls_before = world.calls;
    const Vec3 neutral = clamp.Apply(Vec3::Zero(), Vec3::Zero(), 0.016f, &Query, &world);
    Check(NearEqual(neutral.Magnitude(), 0.0f), "a neutral pose produces no offset");
    Check(world.calls == calls_before, "a neutral pose does not run the query");

    world.blocked = false;
    const Vec3 out = clamp.Apply(Vec3::Zero(), Vec3(0.4f, 0.0f, 0.0f), 0.016f, &Query, &world);
    Check(NearEqual(out.x, 0.4f), "the lean after a neutral pose is not rationed by the old wall");
}

// Regression, found in game rather than here: a lean growing faster than the
// release rate, through open space, was reported as held off geometry on every
// frame. Two causes, both worth a test - the ease ran when nothing had ever
// blocked, and the settle tolerance was absolute so it never arrived in an
// engine whose units are centimetres.
void TestGrowingLeanInOpenSpaceNeverReportsContact() {
    LeanClamp clamp = MakeClamp(10.0f);  // centimetres, as an Unreal mod passes
    World world;
    world.blocked = false;

    // A head sweeping out to a 30cm lean over half a second, which is well
    // inside what a tracker produces and far faster than a 200ms release ease.
    bool followed = true;
    bool everInContact = false;
    for (int frame = 1; frame <= 30; ++frame) {
        const float reach = 30.0f * (static_cast<float>(frame) / 30.0f);
        const Vec3 out = clamp.Apply(Vec3::Zero(), Vec3(reach, 0.0f, 0.0f), 0.016f, &Query, &world);
        if (!NearEqual(out.x, reach, 1e-3f)) followed = false;
        if (clamp.InContact()) everInContact = true;
    }

    Check(followed, "a lean growing through open space is never held back");
    Check(!everInContact, "and is never reported as contact");
}

// The settle has to arrive in centimetres too, not just in metres.
void TestReleaseSettlesInEngineUnits() {
    LeanClamp clamp = MakeClamp(10.0f);
    World world;

    world.blocked = true;
    world.distance = 15.0f;
    clamp.Apply(Vec3::Zero(), Vec3(30.0f, 0.0f, 0.0f), 0.016f, &Query, &world);
    Check(clamp.InContact(), "a 30cm lean into a wall 15cm away is held back");

    world.blocked = false;
    for (int frame = 0; frame < 120; ++frame)
        clamp.Apply(Vec3::Zero(), Vec3(30.0f, 0.0f, 0.0f), 0.016f, &Query, &world);

    Check(!clamp.InContact(), "the release settles within two seconds at centimetre scale");
}

void TestResetDropsTheAllowance() {
    LeanClamp clamp = MakeClamp(0.10f);
    World world;

    world.blocked = true;
    world.distance = 0.12f;
    clamp.Apply(Vec3::Zero(), Vec3(0.4f, 0.0f, 0.0f), 0.016f, &Query, &world);
    Check(clamp.InContact(), "the clamp is in contact before the reset");

    clamp.Reset();
    Check(!clamp.InContact(), "reset clears the contact flag");

    world.blocked = false;
    const Vec3 out = clamp.Apply(Vec3::Zero(), Vec3(0.4f, 0.0f, 0.0f), 0.016f, &Query, &world);
    Check(NearEqual(out.x, 0.4f), "the first lean after a reset takes its answer outright");
}

}  // namespace

int RunLeanClampTests() {
    std::cout << "\nLean clamp tests:\n";
    g_failures = 0;

    TestNullQueryPassesThrough();
    TestClearPathPassesThrough();
    TestBlockedLeanStopsShortBySkin();
    TestDirectionIsPreserved();
    TestNoRoomCollapsesToZero();
    TestTighteningIsInstant();
    TestReleaseIsDamped();
    TestFailedQueryIsReportedNotAbsorbed();
    TestNeutralPoseClearsTheAllowance();
    TestGrowingLeanInOpenSpaceNeverReportsContact();
    TestReleaseSettlesInEngineUnits();
    TestResetDropsTheAllowance();

    if (g_failures == 0) {
        std::cout << "Lean clamp tests: all passed\n";
    } else {
        std::cout << "Lean clamp tests: " << g_failures << " failure(s)\n";
    }
    return g_failures;
}
