// Tests for the aim and marker projections in cameraunlock/rendering/.
//
// Three families, and the tests below cover all of them:
//
//   aim_ndc_projection.h    basis-to-basis, the default. Built here from plain
//                           orthonormal bases rather than any engine angle helper,
//                           so what is pinned is the projection and not a
//                           convention.
//   world_reprojection.h    moves the world point instead of the mark, so the
//                           GAME world-to-screen places it in the drawn camera.
//                           The invariant is a round trip.
//   aim_quat_projection.h   quaternion in, Hor+ FOV scaling.
//
// The Hor+ behaviour is the load-bearing part of the third: SN2 reticle drifted
// ~2x too fast horizontally on 32:9 displays when the projection assumed Vert-
// scaling. These tests pin the model so it can not regress.

#include <cameraunlock/rendering/aim_ndc_projection.h>
#include <cameraunlock/rendering/aim_quat_projection.h>
#include <cameraunlock/rendering/world_reprojection.h>
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


// ---- aim_ndc_projection ------------------------------------------------------
//
// Where the gun points in the picture the head is looking at. The reticle is
// drawn at whatever this returns, so a sign error puts the mark on the wrong side
// of the screen and every shot lands somewhere the player did not point - a
// failure that looks like the camera being wrong rather than the reticle.

// Half-field tangents of a 90-degree horizontal field on 16:9. They are only a
// pair of numbers the projection divides by.
constexpr float kTanX = 0.934f;
constexpr float kTanY = 0.525f;
constexpr float kPi = 3.14159265358979323846f;

struct Basis {
    float fwd[3];
    float right[3];
    float up[3];
};

void RotateAbout(float v[3], const float axis[3], float radians) {
    const float c = std::cos(radians), s = std::sin(radians);
    const float cross[3] = {
        axis[1] * v[2] - axis[2] * v[1],
        axis[2] * v[0] - axis[0] * v[2],
        axis[0] * v[1] - axis[1] * v[0],
    };
    const float d = axis[0] * v[0] + axis[1] * v[1] + axis[2] * v[2];
    for (int i = 0; i < 3; ++i) {
        v[i] = v[i] * c + cross[i] * s + axis[i] * d * (1.0f - c);
    }
}

// +x right, +y up, +z forward - the left-handed convention Unity and Unreal
// use, not a right-handed one. The projection under test never names a
// handedness, so this is only the frame these expectations are written in; a
// port to a right-handed engine flips the signs below rather than reusing them.
// Yaw is positive turning RIGHT, pitch positive looking UP, roll positive about
// the forward axis.
Basis MakeBasis(float yawDeg, float pitchUpDeg, float rollDeg) {
    Basis b = { {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f} };

    const float worldUp[3] = {0.0f, 1.0f, 0.0f};
    RotateAbout(b.fwd, worldUp, yawDeg * kPi / 180.0f);
    RotateAbout(b.right, worldUp, yawDeg * kPi / 180.0f);

    // A positive rotation about the right axis pitches the nose DOWN, so looking
    // up is the negative one.
    RotateAbout(b.fwd, b.right, -pitchUpDeg * kPi / 180.0f);
    RotateAbout(b.up, b.right, -pitchUpDeg * kPi / 180.0f);

    RotateAbout(b.right, b.fwd, rollDeg * kPi / 180.0f);
    RotateAbout(b.up, b.fwd, rollDeg * kPi / 180.0f);
    return b;
}

void NormalizeVec(float v[3]) {
    const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    for (int i = 0; i < 3; ++i) v[i] /= len;
}

bool ProjectFromEye(const float point[3], const float eye[3], const Basis& b,
                    float& x, float& y) {
    using cameraunlock::rendering::ProjectAimToNdc;
    float rel[3];
    for (int i = 0; i < 3; ++i) rel[i] = point[i] - eye[i];
    NormalizeVec(rel);
    return ProjectAimToNdc(rel, b.fwd, b.right, b.up, kTanX, kTanY, x, y);
}

void TestAimAtCentreWhenHeadIsCentred() {
    using cameraunlock::rendering::ProjectAimToNdc;
    std::cout << "Basis-to-basis aim projection:\n";

    const Basis b = MakeBasis(-73.0f, 12.0f, 0.0f);
    float x = 1.0f, y = 1.0f;
    const bool ok = ProjectAimToNdc(b.fwd, b.fwd, b.right, b.up, kTanX, kTanY, x, y);
    Check(ok && Near(x, 0.0f, 1e-5f) && Near(y, 0.0f, 1e-5f),
          "an aim down the drawn forward is the centre of the frame");
}

// Head turns right, so the aim is now to the LEFT of what is on screen. The
// reticle has to follow the gun, not the head.
void TestHeadYawMovesAimOppositeWay() {
    using cameraunlock::rendering::ProjectAimToNdc;
    const Basis clean = MakeBasis(0.0f, 0.0f, 0.0f);
    const Basis drawn = MakeBasis(20.0f, 0.0f, 0.0f);

    float x = 0.0f, y = 0.0f;
    const bool ok = ProjectAimToNdc(clean.fwd, drawn.fwd, drawn.right, drawn.up,
                                    kTanX, kTanY, x, y);
    Check(ok && x < 0.0f && Near(y, 0.0f, 1e-5f)
              && Near(x, -std::tan(20.0f * kPi / 180.0f) / kTanX),
          "a head turned right puts the aim left, by the tangent of the turn");
}

// Head looks up, so the aim sits BELOW the centre of the picture. NDC y is up.
void TestHeadPitchMovesAimDown() {
    using cameraunlock::rendering::ProjectAimToNdc;
    const Basis clean = MakeBasis(40.0f, 0.0f, 0.0f);
    const Basis drawn = MakeBasis(40.0f, 15.0f, 0.0f);

    float x = 0.0f, y = 0.0f;
    const bool ok = ProjectAimToNdc(clean.fwd, drawn.fwd, drawn.right, drawn.up,
                                    kTanX, kTanY, x, y);
    Check(ok && y < 0.0f && Near(x, 0.0f, 1e-5f)
              && Near(y, -std::tan(15.0f * kPi / 180.0f) / kTanY),
          "a head looking up puts the aim below centre");
}

// Roll alone cannot move the aim off the centre - the view spins about the very
// axis the gun points down.
void TestRollAloneLeavesAimAtCentre() {
    using cameraunlock::rendering::ProjectAimToNdc;
    const Basis clean = MakeBasis(116.0f, -8.0f, 0.0f);
    const Basis drawn = MakeBasis(116.0f, -8.0f, 25.0f);

    float x = 1.0f, y = 1.0f;
    const bool ok = ProjectAimToNdc(clean.fwd, drawn.fwd, drawn.right, drawn.up,
                                    kTanX, kTanY, x, y);
    Check(ok && Near(x, 0.0f, 1e-5f) && Near(y, 0.0f, 1e-5f),
          "roll alone leaves the aim at the centre of the frame");
}

// A positional lean moves the eye the frame is drawn from, but not the eye the
// shot comes from. The reticle has to swing by the parallax or it slides off the
// thing the player was aiming at, and the closer the target the further it
// slides. This is the case that needs a hit distance and cannot be answered by
// projecting a direction.
void TestLeanMovesTheAimPointAgainstTheEye() {
    using cameraunlock::rendering::ProjectAimToNdc;
    const Basis b = MakeBasis(0.0f, 0.0f, 0.0f);
    const float distance = 1000.0f;
    const float lean = 12.0f;

    float rel[3];
    for (int i = 0; i < 3; ++i) rel[i] = b.fwd[i] * distance - b.right[i] * lean;
    NormalizeVec(rel);
    float x = 0.0f, y = 0.0f;
    const bool ok = ProjectAimToNdc(rel, b.fwd, b.right, b.up, kTanX, kTanY, x, y);

    float farther[3];
    for (int i = 0; i < 3; ++i) {
        farther[i] = b.fwd[i] * distance * 2.0f - b.right[i] * lean;
    }
    NormalizeVec(farther);
    float x2 = 0.0f, y2 = 0.0f;
    const bool ok2 = ProjectAimToNdc(farther, b.fwd, b.right, b.up, kTanX, kTanY, x2, y2);

    Check(ok && ok2 && Near(x, -(lean / distance) / kTanX) && Near(y, 0.0f, 1e-6f)
              && Near(x2, x * 0.5f) && Near(y2, 0.0f, 1e-6f),
          "the aim swings by the parallax, halving at twice the distance");
}

void TestAimBehindTheViewIsRejected() {
    using cameraunlock::rendering::ProjectAimToNdc;
    const Basis clean = MakeBasis(0.0f, 0.0f, 0.0f);
    const Basis drawn = MakeBasis(120.0f, 0.0f, 0.0f);
    float x = 0.0f, y = 0.0f;
    Check(!ProjectAimToNdc(clean.fwd, drawn.fwd, drawn.right, drawn.up,
                           kTanX, kTanY, x, y),
          "an aim behind the drawn view has no screen position and says so");
}

// ---- world_reprojection ------------------------------------------------------
//
// World-anchored HUD marks are placed by the GAME world-to-screen, which projects
// with the clean camera. Moving the world point instead of the mark makes the
// invariant a round trip: the moved point seen from the CLEAN camera must land
// exactly where the real point lands seen from the DRAWN one.

cameraunlock::rendering::FrameCameras BuildCameras(
    const Basis& clean, const Basis& drawn,
    float leanRight, float leanUp, float leanFwd) {
    cameraunlock::rendering::FrameCameras c = {};
    for (int i = 0; i < 3; ++i) {
        c.cleanFwd[i] = clean.fwd[i];
        c.cleanRight[i] = clean.right[i];
        c.cleanUp[i] = clean.up[i];
        c.drawnFwd[i] = drawn.fwd[i];
        c.drawnRight[i] = drawn.right[i];
        c.drawnUp[i] = drawn.up[i];
        c.cleanEye[i] = 0.0f;
        c.drawnEye[i] = clean.right[i] * leanRight + clean.up[i] * leanUp
                      + clean.fwd[i] * leanFwd;
    }
    return c;
}

void TestReprojectUntrackedFrameIsIdentity() {
    using cameraunlock::rendering::ReprojectWorldPoint;
    std::cout << "World reprojection:\n";

    const Basis clean = MakeBasis(137.0f, 4.0f, -6.0f);
    const auto c = BuildCameras(clean, clean, 0.0f, 0.0f, 0.0f);
    const float p[3] = { 512.0f, -73.0f, 220.0f };
    float moved[3];
    ReprojectWorldPoint(c, p, moved);
    Check(Near(moved[0], p[0], 1e-2f) && Near(moved[1], p[1], 1e-2f)
              && Near(moved[2], p[2], 1e-2f),
          "a frame with no head pose moves nothing");
}

void TestReprojectLandsWhereTheDrawnCameraPutsIt() {
    using cameraunlock::rendering::ReprojectWorldPoint;
    // Combined poses, not one axis at a time: a formula that is right on single
    // axes and wrong on combinations is exactly the bug that survives testing.
    const float poses[][3] = {
        { 25.0f, 0.0f, 0.0f },
        { 0.0f, 12.0f, 0.0f },
        { 0.0f, 0.0f, 20.0f },
        { -18.0f, 10.0f, 14.0f },
    };
    const float cleanAngles[][3] = {
        { 0.0f, 0.0f, 0.0f },
        { 210.0f, -8.0f, 3.0f },
    };
    const float points[][3] = {
        { 900.0f, 0.0f, 0.0f },
        { 300.0f, 450.0f, 120.0f },
        { -600.0f, 200.0f, -80.0f },
    };

    bool ok = true;
    int compared = 0;
    for (const auto& ang : cleanAngles) {
        const Basis clean = MakeBasis(ang[0], ang[1], ang[2]);
        for (const auto& pose : poses) {
            const Basis drawn = MakeBasis(ang[0] + pose[0], ang[1] + pose[1],
                                          ang[2] + pose[2]);
            const auto c = BuildCameras(clean, drawn, 9.0f, -4.0f, 6.0f);
            for (const auto& p : points) {
                float wantX = 0.0f, wantY = 0.0f;
                if (!ProjectFromEye(p, c.drawnEye, drawn, wantX, wantY)) {
                    continue;   // behind the drawn view: nothing to agree about
                }
                float moved[3];
                ReprojectWorldPoint(c, p, moved);
                float gotX = 0.0f, gotY = 0.0f;
                ok = ok && ProjectFromEye(moved, c.cleanEye, clean, gotX, gotY)
                     && Near(gotX, wantX, 1e-3f) && Near(gotY, wantY, 1e-3f);
                ++compared;
            }
        }
    }
    Check(ok && compared > 0,
          "the moved point lands where the drawn camera put the real one");
}

void TestReprojectKeepsItsDistance() {
    using cameraunlock::rendering::ReprojectWorldPoint;
    // Depth has to survive the move, or the behind-the-camera test the callers
    // branch on answers about a point that is not where the marker is.
    const Basis clean = MakeBasis(40.0f, 5.0f, 0.0f);
    const Basis drawn = MakeBasis(70.0f, -2.0f, 11.0f);
    const auto c = BuildCameras(clean, drawn, 12.0f, 3.0f, -5.0f);
    const float p[3] = { 250.0f, -400.0f, 60.0f };
    float moved[3];
    ReprojectWorldPoint(c, p, moved);

    float before = 0.0f, after = 0.0f;
    for (int i = 0; i < 3; ++i) {
        const float a = p[i] - c.drawnEye[i];
        const float b = moved[i] - c.cleanEye[i];
        before += a * a;
        after += b * b;
    }
    Check(Near(std::sqrt(after), std::sqrt(before), 1e-2f),
          "depth survives the move, so the behind-the-camera test still answers");
}

}  // namespace

int RunProjectionTests() {
    std::cout << "Aim projection tests\n";

    TestIdentityCentred();
    TestYawMovesHorizontally();
    TestHorPlusScaling();
    TestAimAtCentreWhenHeadIsCentred();
    TestHeadYawMovesAimOppositeWay();
    TestHeadPitchMovesAimDown();
    TestRollAloneLeavesAimAtCentre();
    TestLeanMovesTheAimPointAgainstTheEye();
    TestAimBehindTheViewIsRejected();
    TestReprojectUntrackedFrameIsIdentity();
    TestReprojectLandsWhereTheDrawnCameraPutsIt();
    TestReprojectKeepsItsDistance();

    if (g_failures == 0) {
        std::cout << "Projection tests: all passed\n";
    } else {
        std::cout << "Projection tests: " << g_failures << " failure(s)\n";
    }
    return g_failures;
}
