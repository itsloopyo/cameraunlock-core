// Tests for the quat-relative Hor+ aim projection
// (cameraunlock/rendering/aim_quat_projection.h).
//
// The Hor+ behaviour is the load-bearing part: SN2's reticle drifted ~2x too
// fast horizontally on 32:9 displays when the projection assumed Vert-
// scaling. These tests pin the model so it can't regress.

#include <cameraunlock/rendering/aim_quat_projection.h>
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

bool Near(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

void TestIdentityCentred() {
    using cameraunlock::rendering::ProjectAimQuatHorPlus;
    std::cout << "Identity projection:\n";

    const auto p = ProjectAimQuatHorPlus(0, 0, 0, 1, 1920, 1080, 90.0f);
    Check(p.inFront, "identity is in front");
    Check(Near(p.screenX, 960) && Near(p.screenY, 540),
          "identity projects to screen centre");
    Check(Near(p.ndcX, 0) && Near(p.ndcY, 0), "identity NDC is (0,0)");
}

void TestYawMovesHorizontally() {
    using cameraunlock::rendering::ProjectAimQuatHorPlus;
    using namespace cameraunlock::unreal;
    std::cout << "Pure-axis movement:\n";

    // 20deg relative yaw moves the projection horizontally only.
    const FQuat4d yawQ = QuatFromEulerDeg(0, 20, 0);
    const auto py = ProjectAimQuatHorPlus(yawQ.X, yawQ.Y, yawQ.Z, yawQ.W,
                                          1920, 1080, 90.0f);
    Check(py.inFront, "20deg yaw is in front");
    Check(!Near(py.screenX, 960, 1.0f) && Near(py.screenY, 540, 1.0f),
          "yaw moves horizontally, not vertically");

    // 20deg relative pitch moves the projection vertically only.
    const FQuat4d pitchQ = QuatFromEulerDeg(20, 0, 0);
    const auto pp = ProjectAimQuatHorPlus(pitchQ.X, pitchQ.Y, pitchQ.Z, pitchQ.W,
                                          1920, 1080, 90.0f);
    Check(pp.inFront, "20deg pitch is in front");
    Check(Near(pp.screenX, 960, 1.0f) && !Near(pp.screenY, 540, 1.0f),
          "pitch moves vertically, not horizontally");

    // Aim behind the view (depth <= 0) reports not-in-front.
    const FQuat4d backQ = QuatFromEulerDeg(0, 175, 0);
    const auto pb = ProjectAimQuatHorPlus(backQ.X, backQ.Y, backQ.Z, backQ.W,
                                          1920, 1080, 90.0f);
    Check(!pb.inFront, "aim behind view reports inFront=false");
}

void TestHorPlusScaling() {
    using cameraunlock::rendering::ProjectAimQuatHorPlus;
    using namespace cameraunlock::unreal;
    std::cout << "Hor+ aspect scaling:\n";

    const FQuat4d yawQ = QuatFromEulerDeg(0, 20, 0);

    // At 16:9 the NDC offset for a given yaw.
    const auto p16x9 = ProjectAimQuatHorPlus(yawQ.X, yawQ.Y, yawQ.Z, yawQ.W,
                                             1920, 1080, 90.0f);
    // At 32:9 (double width) the horizontal tangent range doubles under Hor+,
    // so the same yaw lands at half the NDC offset.
    const auto p32x9 = ProjectAimQuatHorPlus(yawQ.X, yawQ.Y, yawQ.Z, yawQ.W,
                                             3840, 1080, 90.0f);
    Check(Near(p32x9.ndcX, p16x9.ndcX * 0.5f, 1e-4f),
          "32:9 halves the NDC offset for the same yaw (Hor+, not Vert-)");

    // Vertical NDC for a pitch rotation is aspect-independent under Hor+.
    const FQuat4d pitchQ = QuatFromEulerDeg(20, 0, 0);
    const auto v16x9 = ProjectAimQuatHorPlus(pitchQ.X, pitchQ.Y, pitchQ.Z, pitchQ.W,
                                             1920, 1080, 90.0f);
    const auto v32x9 = ProjectAimQuatHorPlus(pitchQ.X, pitchQ.Y, pitchQ.Z, pitchQ.W,
                                             3840, 1080, 90.0f);
    Check(Near(v16x9.ndcY, v32x9.ndcY, 1e-4f),
          "vertical NDC is aspect-independent under Hor+");

    // Extreme yaw clamps NDC to the viewport edge rather than overshooting.
    const FQuat4d bigYawQ = QuatFromEulerDeg(0, 85, 0);
    const auto pc = ProjectAimQuatHorPlus(bigYawQ.X, bigYawQ.Y, bigYawQ.Z, bigYawQ.W,
                                          1920, 1080, 90.0f);
    Check(pc.inFront && std::fabs(pc.ndcX) == 1.0f,
          "near-90deg yaw clamps NDC to the screen edge");
}

}  // namespace

int RunProjectionTests() {
    std::cout << "Aim projection tests\n";

    TestIdentityCentred();
    TestYawMovesHorizontally();
    TestHorPlusScaling();

    if (g_failures == 0) {
        std::cout << "Projection tests: all passed\n";
    } else {
        std::cout << "Projection tests: " << g_failures << " failure(s)\n";
    }
    return g_failures;
}
