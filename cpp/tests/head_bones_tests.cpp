// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
// The neck and head bones turned by the tracked head
// (cameraunlock/camera/head_bones.h), ported from Headstart's
// native/unreal/tests/discovery_tests.cpp.

#include <cameraunlock/camera/head_bones.h>

#include <cmath>
#include <iostream>
#include <vector>

namespace {

using namespace cameraunlock::unreal;
namespace hb = cameraunlock::camera::head_bones;

int g_failures = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

bool Near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }

void TestWorldTurn() {
    std::cout << "WorldTurn:\n";
    const FRotator yawed = QuatToRotator(hb::WorldTurn(90, 30, 0, 0, 1));
    Check(Near(yawed.Yaw, 30) && Near(yawed.Pitch, 0) && Near(yawed.Roll, 0),
          "a yaw is about world up, whichever way the body faces");
    const FVector nose = QuatRotateVec(hb::WorldTurn(90, 0, 20, 0, 1), {0, 1, 0});
    Check(Near(nose.X, 0) && Near(nose.Z, std::sin(20 * 3.14159265358979 / 180)),
          "a pitch is about the body's own right, so facing +Y it tips the head over the Y axis");
    Check(Near(QuatToRotator(hb::WorldTurn(0, 0, 0, 20, 1)).Roll, -20), "the roll is negated, as the view's is");
    Check(Near(QuatToRotator(hb::WorldTurn(0, 60, 0, 0, 2)).Yaw, hb::kYawLimit), "held to the yaw limit after the multiplier");
    Check(Near(QuatToRotator(hb::WorldTurn(0, 0, -70, 0, 1)).Pitch, -hb::kDownLimit), "held to the down limit");
    Check(Near(QuatToRotator(hb::WorldTurn(0, 0, 70, 0, 1)).Pitch, hb::kUpLimit), "held to the up limit");
    Check(Near(QuatToRotator(hb::WorldTurn(0, 0, 0, 60, 1)).Roll, -hb::kRollLimit), "held to the roll limit");

    const FQuat4d mesh = QuatFromEulerDeg(0, -90, 0);
    Check(Near(QuatToRotator(hb::InComponent(QuatFromEulerDeg(0, 30, 0), mesh)).Yaw, 30),
          "a mesh turned -90 about Z sees a world yaw as the same yaw about its own up");
}

void TestTurnNeckAndHead() {
    std::cout << "TurnNeckAndHead:\n";
    // root(0) -> spine(1) -> neck(2) -> head(3) -> eye(4); hand(5) under the spine.
    std::vector<hb::Xf> pose = {
        {{0, 0, 0, 1}, {0, 0, 0}},   {{0, 0, 0, 1}, {0, 0, 100}},  {{0, 0, 0, 1}, {0, 0, 150}},
        {{0, 0, 0, 1}, {0, 0, 160}}, {{0, 0, 0, 1}, {10, 0, 165}}, {{0, 0, 0, 1}, {20, 30, 120}},
    };
    const std::vector<int> parent = {-1, 0, 1, 2, 3, 1};
    const std::vector<hb::Xf> before = pose;
    const std::vector<bool> moved = hb::TurnNeckAndHead(pose, parent, 2, 3, QuatFromEulerDeg(0, 90, 0), hb::kNeckShare);
    Check(!moved[0] && !moved[1] && moved[2] && moved[3] && moved[4] && !moved[5], "the neck and everything below it move");
    Check(Near(pose[2].t.Z, 150) && Near(QuatToRotator(pose[2].q).Yaw, 36), "the neck turns in place by its 40%");
    Check(Near(QuatToRotator(pose[3].q).Yaw, 90), "the head ends up turned by the whole 90");
    Check(Near(pose[3].t.X, 0) && Near(pose[3].t.Y, 0) && Near(pose[3].t.Z, 160),
          "the head straight above the neck keeps its place");
    Check(Near(pose[4].t.X, 0) && Near(pose[4].t.Y, 10) && Near(pose[4].t.Z, 165),
          "the eye in front of it swings round to the side");
    Check(Near(pose[5].t.X, before[5].t.X) && Near(pose[5].t.Y, before[5].t.Y), "a hand off the spine stays");

    // Compose and Inverse undo each other.
    const hb::Xf a{QuatFromEulerDeg(10, 20, 30), {1, 2, 3}};
    const hb::Xf roundTrip = hb::Compose(a, hb::Inverse(a));
    Check(Near(roundTrip.q.W * roundTrip.q.W, 1, 1e-12) && Near(roundTrip.t.X, 0, 1e-12) && Near(roundTrip.t.Y, 0, 1e-12) &&
              Near(roundTrip.t.Z, 0, 1e-12),
          "a transform after its inverse is the identity");
}

}  // namespace

int RunHeadBonesTests() {
    std::cout << "Head bone tests\n";
    TestWorldTurn();
    TestTurnNeckAndHead();
    if (g_failures == 0) {
        std::cout << "Head bone tests: all passed\n";
    } else {
        std::cout << "Head bone tests: " << g_failures << " failure(s)\n";
    }
    return g_failures;
}
