// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
// The rotation helpers Headstart's backends each carried a copy of: a
// quaternion from a camera's axes, a rotator from its axes and back, a turn
// scaled about its own axis, slerp, the shortest arc and a rotation from a
// forward and an up (cameraunlock/unreal/ue_math.h), and the float
// Quat4::FromBasis (cameraunlock/math/quat4.h). Each is held to the engine
// convention QuatFromEulerDeg and FromYawPitchRoll already pin.

#include <cameraunlock/math/quat4.h>
#include <cameraunlock/unreal/ue_math.h>

#include <cmath>
#include <iostream>

namespace {

using namespace cameraunlock::unreal;

int g_failures = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

bool Near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }

bool SameTurn(const FQuat4d& a, const FQuat4d& b, double eps = 1e-9) {
    const double dot = a.X * b.X + a.Y * b.Y + a.Z * b.Z + a.W * b.W;
    return Near(std::fabs(dot), 1.0, eps);
}

bool SameVec(const FVector& a, const FVector& b, double eps = 1e-9) {
    return Near(a.X, b.X, eps) && Near(a.Y, b.Y, eps) && Near(a.Z, b.Z, eps);
}

void Axes(const FQuat4d& q, FVector& forward, FVector& right, FVector& up) {
    forward = QuatRotateVec(q, {1, 0, 0});
    right = QuatRotateVec(q, {0, 1, 0});
    up = QuatRotateVec(q, {0, 0, 1});
}

const FRotator kRotators[] = {
    {0, 0, 0},     {23, -47, 11},  {0, 180, 0},   {80, 170, 170}, {-60, -120, 95},
    {10, 90, 179}, {-89, 30, -40}, {45, 0, -170}, {0, -179, 0},   {5, 175, -178},
};

void TestQuatFromAxes() {
    std::cout << "QuatFromAxes:\n";
    bool all = true;
    for (const FRotator& r : kRotators) {
        const FQuat4d q = QuatFromEulerDeg(r.Pitch, r.Yaw, r.Roll);
        FVector f, rt, u;
        Axes(q, f, rt, u);
        const FQuat4d back = QuatFromAxes(f, rt, u);
        all = all && SameTurn(back, q, 1e-12) &&
              Near(back.X * back.X + back.Y * back.Y + back.Z * back.Z + back.W * back.W, 1.0, 1e-12);
    }
    Check(all, "the columns forward, right and up give back the rotation, on every branch of Shepperd's method");

    const FQuat4d id = QuatFromAxes({1, 0, 0}, {0, 1, 0}, {0, 0, 1});
    Check(Near(id.X, 0) && Near(id.Y, 0) && Near(id.Z, 0) && Near(id.W, 1), "the unit axes are the identity");

    // Columns, not rows: a yaw of 90 takes +X to +Y, so the X column is (0, 1, 0).
    const FQuat4d yaw = QuatFromAxes({0, 1, 0}, {-1, 0, 0}, {0, 0, 1});
    Check(SameTurn(yaw, QuatFromEulerDeg(0, 90, 0)), "the arguments are the columns: where +X, +Y and +Z go");

}

void TestRotatorAxes() {
    std::cout << "RotatorFromAxes / AxesFromRotator:\n";
    bool same = true;
    for (const FRotator& r : kRotators) {
        FVector f, rt, u, qf, qr, qu;
        AxesFromRotator(r, f, rt, u);
        Axes(QuatFromEulerDeg(r.Pitch, r.Yaw, r.Roll), qf, qr, qu);
        same = same && SameVec(f, qf, 1e-12) && SameVec(rt, qr, 1e-12) && SameVec(u, qu, 1e-12);
    }
    Check(same, "AxesFromRotator is QuatFromEulerDeg's rotation, axis for axis");

    bool roundTrip = true;
    for (const FRotator& r : kRotators) {
        if (std::fabs(r.Pitch) > 85) continue;
        FVector f, rt, u;
        AxesFromRotator(r, f, rt, u);
        const FRotator back = RotatorFromAxes(f, rt, u);
        roundTrip = roundTrip && Near(back.Pitch, r.Pitch, 1e-9) && Near(back.Yaw, r.Yaw, 1e-9) && Near(back.Roll, r.Roll, 1e-9);
    }
    Check(roundTrip, "RotatorFromAxes gives back the rotator away from the poles");

    FVector f, rt, u;
    AxesFromRotator({-89, 30, -40}, f, rt, u);
    const FRotator pole = RotatorFromAxes(f, rt, u);
    FVector pf, pr, pu;
    AxesFromRotator(pole, pf, pr, pu);
    Check(Near(pole.Pitch, -89, 1e-9) && SameVec(pf, f, 1e-9) && SameVec(pr, rt, 1e-9) && SameVec(pu, u, 1e-9),
          "near the pole the rotator it reads still builds the same axes");
    AxesFromRotator({90, 30, -40}, f, rt, u);
    Check(Near(RotatorFromAxes(f, rt, u).Pitch, 90, 1e-9), "at the pole the pitch is read off forward alone");
}

void TestScaleTurn() {
    std::cout << "QuatScaleTurn:\n";
    const FQuat4d yaw40 = QuatFromEulerDeg(0, 40, 0);
    Check(Near(QuatToRotator(QuatScaleTurn(yaw40, 1.5)).Yaw, 60, 1e-9), "a yaw of 40 times 1.5 is a yaw of 60");
    const FQuat4d flipped{-yaw40.X, -yaw40.Y, -yaw40.Z, -yaw40.W};
    Check(Near(QuatToRotator(QuatScaleTurn(flipped, 1.5)).Yaw, 60, 1e-9),
          "the same turn in the other hemisphere scales the short way round");
    const FQuat4d none = QuatScaleTurn({0, 0, 0, 1}, 3);
    Check(Near(none.W, 1) && Near(none.X, 0), "no turn stays no turn");
    const FQuat4d zero = QuatScaleTurn(QuatFromEulerDeg(10, 30, -5), 0);
    Check(Near(zero.W, 1) && Near(zero.X, 0) && Near(zero.Y, 0) && Near(zero.Z, 0), "a multiplier of 0 is the identity");
    const FQuat4d q = QuatFromEulerDeg(12, -33, 7);
    Check(SameTurn(QuatScaleTurn(q, 1), q, 1e-12), "a multiplier of 1 is the turn itself");
    const FQuat4d half = QuatScaleTurn(q, 0.5);
    Check(SameTurn(QuatMul(half, half), q, 1e-12), "half a turn twice is the turn, about the same axis");
}

void TestSlerpAndArcs() {
    std::cout << "QuatSlerp, QuatShortestArc, QuatFromForwardUp, QuatNormalized:\n";
    const FQuat4d a = QuatFromEulerDeg(0, 0, 0), b = QuatFromEulerDeg(0, 90, 0);
    Check(SameTurn(QuatSlerp(a, b, 0), a) && SameTurn(QuatSlerp(a, b, 1), b, 1e-12), "slerp runs from a to b");
    Check(Near(QuatToRotator(QuatSlerp(a, b, 0.5)).Yaw, 45, 1e-9), "halfway from yaw 0 to yaw 90 is yaw 45");
    Check(Near(QuatToRotator(QuatSlerp(a, {-b.X, -b.Y, -b.Z, -b.W}, 0.5)).Yaw, 45, 1e-9),
          "the other hemisphere's b slerps the short way too");
    const FQuat4d c = QuatFromEulerDeg(0, 0.01, 0);
    const FQuat4d blended = QuatSlerp(a, c, 0.5);
    Check(Near(QuatToRotator(blended).Yaw, 0.005, 1e-9) &&
              Near(blended.X * blended.X + blended.Y * blended.Y + blended.Z * blended.Z + blended.W * blended.W, 1, 1e-12),
          "nearly equal turns blend linearly and stay unit length");

    const FQuat4d arc = QuatShortestArc({1, 0, 0}, {0, 1, 0});
    Check(SameVec(QuatRotateVec(arc, {1, 0, 0}), {0, 1, 0}, 1e-12) && SameTurn(arc, QuatFromEulerDeg(0, 90, 0), 1e-12),
          "the shortest arc from +X to +Y is a yaw of 90");
    const FQuat4d opposite = QuatShortestArc({1, 0, 0}, {-1, 0, 0});
    Check(Near(opposite.W, 1) && Near(opposite.X, 0), "a half turn has no one axis and is the identity");

    const FRotator facing = QuatToRotator(QuatFromForwardUp({3, 3, 0}, {0, 0, 1}));
    Check(Near(facing.Yaw, 45, 1e-9) && Near(facing.Pitch, 0, 1e-9) && Near(facing.Roll, 0, 1e-9),
          "a forward of any length and the world up give the level heading");
    const FRotator tilted = QuatToRotator(QuatFromForwardUp({1, 0, 0}, {0, 0.3, 1}));
    Check(Near(tilted.Yaw, 0, 1e-9) && Near(tilted.Pitch, 0, 1e-9) &&
              Near(tilted.Roll, std::atan2(0.3, 1.0) * 180 / 3.14159265358979323846, 1e-9),
          "an up leaning right rolls the right side down by its angle; forward is kept exactly");
    const FQuat4d noForward = QuatFromForwardUp({0, 0, 0}, {0, 0, 1});
    const FQuat4d upAlong = QuatFromForwardUp({0, 0, 2}, {0, 0, 1});
    Check(Near(noForward.W, 1) && Near(upAlong.W, 1), "no forward, or an up along it, gives the identity");

    const FQuat4d n = QuatNormalized({0, 0, 3, 4});
    Check(Near(n.Z, 0.6) && Near(n.W, 0.8), "QuatNormalized scales to unit length");
}

void TestQuat4FromBasis() {
    std::cout << "Quat4::FromBasis:\n";
    using cameraunlock::math::Quat4;
    using cameraunlock::math::Vec3;
    const Quat4 id = Quat4::FromBasis({1, 0, 0}, {0, 1, 0}, {0, 0, 1});
    Check(std::fabs(id.w - 1) < 1e-6f && std::fabs(id.x) < 1e-6f, "the unit axes are the identity");
    bool all = true;
    const float cases[][3] = {{30, 10, 5}, {0, 180, 0}, {180, 0, 0}, {170, 80, 170}, {-120, -60, 95}, {90, 0, 179}, {0, 0, 180}};
    for (const auto& c : cases) {
        const Quat4 q = Quat4::FromYawPitchRoll(c[0], c[1], c[2]);
        const Quat4 back = Quat4::FromBasis(q.Rotate(Vec3(1, 0, 0)), q.Rotate(Vec3(0, 1, 0)), q.Rotate(Vec3(0, 0, 1)));
        all = all && std::fabs(std::fabs(back.Dot(q)) - 1.0f) < 1e-5f;
    }
    Check(all, "the rotated unit axes, as columns, give back the rotation on every branch");
}

}  // namespace

int RunQuatHelpersTests() {
    std::cout << "Quaternion helper tests\n";
    TestQuatFromAxes();
    TestRotatorAxes();
    TestScaleTurn();
    TestSlerpAndArcs();
    TestQuat4FromBasis();
    if (g_failures == 0) {
        std::cout << "Quaternion helper tests: all passed\n";
    } else {
        std::cout << "Quaternion helper tests: " << g_failures << " failure(s)\n";
    }
    return g_failures;
}
