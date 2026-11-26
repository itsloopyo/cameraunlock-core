// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
// The carried light's turn from the head as rotations
// (cameraunlock/effects/follow_turn.h). CarryTurn's cases are ported from
// Headstart's native/re/tests/frame_tests.cpp and native/4a/tests.

#include <cameraunlock/effects/follow_turn.h>

#include <cmath>
#include <iostream>

namespace {

using namespace cameraunlock::unreal;
namespace fx = cameraunlock::effects;

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
    return Near(std::fabs(a.X * b.X + a.Y * b.Y + a.Z * b.Z + a.W * b.W), 1.0, eps);
}

FQuat4d Euler(double pitch, double yaw, double roll) { return QuatFromEulerDeg(pitch, yaw, roll); }

void TestFollowTurn() {
    std::cout << "FollowTurn:\n";
    const FQuat4d clean = Euler(0, 30, 0), posed = Euler(0, 50, 0);
    Check(Near(QuatToRotator(QuatMul(fx::FollowTurn(clean, posed, 1.5), clean)).Yaw, 60),
          "a head yaw of 20 led by 1.5 turns the light 30 from the clean view");

    const FQuat4d c = Euler(10, 30, 5), p = Euler(20, 60, -10);
    Check(SameTurn(QuatMul(fx::FollowTurn(c, p, 1.0), c), p), "led by 1 the clean view turns onto the posed one");
    const FQuat4d none = fx::FollowTurn(c, p, 0.0);
    Check(Near(none.W * none.W, 1), "led by 0 there is no turn");
    const FQuat4d still = fx::FollowTurn(c, c, 1.5);
    Check(Near(still.W * still.W, 1), "no head turn is no turn");

    // The same as scaling the world turn posed * clean^-1 and laying it on the clean view.
    const FQuat4d world = QuatMul(QuatScaleTurn(QuatMul(p, QuatInv(c)), 1.5), c);
    Check(SameTurn(QuatMul(fx::FollowTurn(c, p, 1.5), c), world, 1e-12),
          "the head's turn scaled in the clean frame is its world turn scaled");

    const FQuat4d level = Euler(0, 0, 0);
    Check(Near(QuatToRotator(QuatMul(fx::FollowTurn(level, Euler(0, 80, 0), 1.5, 3.14159265358979323846 / 2), level)).Yaw, 90),
          "held to a quarter turn, a head yaw of 80 led by 1.5 turns the light 90");
    Check(Near(QuatToRotator(QuatMul(fx::FollowTurn(level, Euler(0, 100, 0), 1.5, 3.14159265358979323846 / 2), level)).Yaw, 100),
          "a head turned past the limit is followed 1:1");
    Check(Near(QuatToRotator(QuatMul(fx::FollowTurn(level, Euler(0, 40, 0), 1.5, 3.14159265358979323846 / 2), level)).Yaw, 60),
          "inside the limit the multiplier stands");
}

struct Rows {
    float m[3][4];
};

Rows FromRotator(const FRotator& r) {
    FVector f, rt, u;
    AxesFromRotator(r, f, rt, u);
    const FVector axes[3] = {f, rt, u};
    Rows out{};
    for (int i = 0; i < 3; ++i) {
        out.m[i][0] = static_cast<float>(axes[i].X);
        out.m[i][1] = static_cast<float>(axes[i].Y);
        out.m[i][2] = static_cast<float>(axes[i].Z);
    }
    return out;
}

bool SameBasis(const Rows& a, const Rows& b) {
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            if (!Near(a.m[i][j], b.m[i][j], 1e-5)) return false;
    return true;
}

void TestCarryTurn() {
    std::cout << "CarryTurn:\n";
    const Rows clean = FromRotator({5, 30, 0});
    const Rows drawn = FromRotator({5, 50, 0});
    Rows light = clean;
    light.m[0][3] = 7.f;
    fx::CarryTurn(light.m, clean.m, drawn.m, 1.0);
    Check(SameBasis(light, drawn) && light.m[0][3] == 7.f,
          "a light lined up with the camera takes the camera's turn and keeps its fourth float");

    const Rows level = FromRotator({0, 0, 0});
    Rows led = level;
    fx::CarryTurn(led.m, level.m, FromRotator({0, 20, 0}).m, 1.5);
    Check(SameBasis(led, FromRotator({0, 30, 0})), "a yaw of 20 times 1.5 is a yaw of 30");
    Check(Near(fx::BasisTurnDegrees(level.m, led.m), 30, 1e-3), "and BasisTurnDegrees reads it as 30");

    Rows still = FromRotator({-10, 100, 20});
    const Rows before = still;
    fx::CarryTurn(still.m, clean.m, drawn.m, 0.0);
    Check(SameBasis(still, before), "a multiplier of 0 leaves the light alone");
    fx::CarryTurn(still.m, clean.m, clean.m, 1.5);
    Check(SameBasis(still, before), "no turn leaves the light alone");

    Rows held = FromRotator({0, 10, 0});
    fx::CarryTurn(held.m, level.m, FromRotator({0, 25, 0}).m, 1.0);
    Check(SameBasis(held, FromRotator({0, 35, 0})), "a light 10 off the view stays 10 off it");

    Rows scaled = level;
    for (int j = 0; j < 3; ++j) scaled.m[0][j] *= 2.f;
    fx::CarryTurn(scaled.m, level.m, FromRotator({0, 20, 0}).m, 1.0);
    const double len = std::sqrt(double(scaled.m[0][0]) * scaled.m[0][0] + double(scaled.m[0][1]) * scaled.m[0][1] +
                                 double(scaled.m[0][2]) * scaled.m[0][2]);
    Check(Near(len, 2.0, 1e-5), "a scaled row keeps its length");

    Rows wide = level;
    fx::CarryTurn(wide.m, level.m, FromRotator({0, 40, 0}).m, 1.5);
    Check(Near(fx::BasisTurnDegrees(level.m, wide.m), 60, 1e-3), "led by 1.5, a 40 degree head turn takes it 60");
}

}  // namespace

int RunFollowTurnTests() {
    std::cout << "Follow turn tests\n";
    TestFollowTurn();
    TestCarryTurn();
    if (g_failures == 0) {
        std::cout << "Follow turn tests: all passed\n";
    } else {
        std::cout << "Follow turn tests: " << g_failures << " failure(s)\n";
    }
    return g_failures;
}
