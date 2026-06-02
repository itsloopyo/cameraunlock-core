// Tests for the UE5 LWC math helpers (cameraunlock/unreal/ue_math.h).
//
// The conversions are byte-for-byte ports of UE5's FRotator::Quaternion() and
// FQuat::Rotator(); these tests pin the conventions (axis order, signs,
// singularity handling) so a future "simplification" can't silently flip a
// sign and rotate every UE mod's camera the wrong way.

#include <cameraunlock/unreal/ue_math.h>

#include <cmath>
#include <iostream>

namespace {

int g_failures = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

bool Near(double a, double b, double eps = 1e-9) {
    return std::fabs(a - b) <= eps;
}

void TestEulerQuatRoundTrip() {
    using namespace cameraunlock::unreal;
    std::cout << "Euler <-> Quat round trip:\n";

    // Identity.
    const FQuat4d qi = QuatFromEulerDeg(0, 0, 0);
    Check(Near(qi.X, 0) && Near(qi.Y, 0) && Near(qi.Z, 0) && Near(qi.W, 1),
          "zero rotation is identity quaternion");

    // Round-trip a generic rotation well away from the gimbal singularity.
    const double pitch = 23.0, yaw = -47.0, roll = 11.0;
    const FRotator r = QuatToRotator(QuatFromEulerDeg(pitch, yaw, roll));
    Check(Near(r.Pitch, pitch, 1e-9) && Near(r.Yaw, yaw, 1e-9) && Near(r.Roll, roll, 1e-9),
          "pitch/yaw/roll survive quat round trip");

    // Pure yaw round trip keeps the other axes at zero.
    const FRotator ry = QuatToRotator(QuatFromEulerDeg(0, 90, 0));
    Check(Near(ry.Yaw, 90, 1e-9) && Near(ry.Pitch, 0, 1e-9) && Near(ry.Roll, 0, 1e-9),
          "pure 90deg yaw round trips clean");

    // Near-gimbal pitch resolves to the +/-90 singularity branch without NaNs.
    const FRotator rg = QuatToRotator(QuatFromEulerDeg(90, 0, 0));
    Check(Near(rg.Pitch, 90, 1e-3) && rg.Yaw == rg.Yaw && rg.Roll == rg.Roll,
          "+90deg pitch hits singularity branch without NaN");
}

void TestQuatAlgebra() {
    using namespace cameraunlock::unreal;
    std::cout << "Quat algebra:\n";

    const FQuat4d q = QuatFromEulerDeg(10, 20, 30);
    const FQuat4d qq = QuatMul(q, QuatInv(q));
    Check(Near(qq.X, 0) && Near(qq.Y, 0) && Near(qq.Z, 0) && Near(qq.W, 1),
          "q * q^-1 == identity");

    // Rotating UE-forward (+X) by pure yaw stays in the XY plane.
    const FQuat4d yaw90 = QuatFromEulerDeg(0, 90, 0);
    const FVector fwd = QuatRotateVec(yaw90, FVector{1, 0, 0});
    Check(Near(fwd.X, 0, 1e-9) && Near(std::fabs(fwd.Y), 1, 1e-9) && Near(fwd.Z, 0, 1e-9),
          "90deg yaw rotates +X into the Y axis");

    // Rotating by identity is a no-op.
    const FVector v = QuatRotateVec(FQuat4d{0, 0, 0, 1}, FVector{1, 2, 3});
    Check(Near(v.X, 1) && Near(v.Y, 2) && Near(v.Z, 3), "identity rotation is a no-op");
}

}  // namespace

int RunUnrealMathTests() {
    std::cout << "Unreal LWC math tests\n";

    TestEulerQuatRoundTrip();
    TestQuatAlgebra();

    if (g_failures == 0) {
        std::cout << "Unreal math tests: all passed\n";
    } else {
        std::cout << "Unreal math tests: " << g_failures << " failure(s)\n";
    }
    return g_failures;
}
