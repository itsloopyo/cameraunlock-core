// Tests for the RE Engine boundary maths.
//
// The one these exist for is the direction of the head-translation offset.
// Negative z is the forward lean everywhere inside the pipeline and the clamp
// is asymmetric on that basis - 0.40m forward, 0.10m back. RE Engine's
// camera-local +z is forward, so the boundary has to negate. It shipped without
// the negation, which inverts the lean and swaps the two budgets over, and
// nothing failed: the camera still moved, just the wrong way on the wrong
// allowance. A test is the only thing that catches that.

#include <cameraunlock/processing/position_processor.h>
#include <cameraunlock/reframework/re_math.h>

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

bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// Canvas comparisons run in pixels on a 1920x1080 reference canvas, where a
// hundred-pixel value carries more float error than the 1e-4 above allows.
// A twentieth of a pixel is well inside anything a player could see.
bool NearPixels(float a, float b) { return std::fabs(a - b) < 0.05f; }

// Identity camera: rows 0/1/2 are right/up/forward, row 3 is the position.
cameraunlock::reframework::Matrix4x4f IdentityCamera() {
    cameraunlock::reframework::Matrix4x4f m{};
    m.m[0][0] = 1.0f;
    m.m[1][1] = 1.0f;
    m.m[2][2] = 1.0f;
    m.m[3][3] = 1.0f;
    return m;
}

// The pipeline output for a tracker held at a full-scale lean, run to steady
// state so smoothing is settled.
float SteadyLean(float rawZ, bool invertZ) {
    cameraunlock::PositionProcessor processor;
    cameraunlock::PositionSettings settings;
    settings.invert_z = invertZ;
    processor.SetSettings(settings);

    const cameraunlock::math::Quat4 upright(0.0f, 0.0f, 0.0f, 1.0f);
    cameraunlock::math::Vec3 out;
    for (int i = 0; i < 600; ++i) {
        out = processor.Process(cameraunlock::PositionData(0.0f, 0.0f, rawZ), upright, 0.016f);
    }
    return out.z;
}

// World-space z the camera ends at when the pipeline hands the boundary an
// offset, with the camera looking down world +z.
float AppliedWorldZ(float offsetZ) {
    using namespace cameraunlock::reframework;
    Matrix4x4f world = IdentityCamera();
    const Matrix4x4f axes = world;
    ApplyViewSpacePositionOffset(world, axes, 0.0f, 0.0f, offsetZ);
    return world.m[3][2];
}

void TestForwardLeanMovesTheCameraForward() {
    std::cout << "ApplyViewSpacePositionOffset direction:\n";

    // The camera's forward is +z in world space here, so a forward lean has to
    // raise the world z. The pipeline expresses that lean as NEGATIVE z.
    Check(AppliedWorldZ(-0.40f) > 0.0f,
          "a negative pipeline z (forward lean) moves the camera along its forward axis");
    Check(AppliedWorldZ(0.10f) < 0.0f,
          "a positive pipeline z (backward lean) moves the camera against its forward axis");
    Check(Near(AppliedWorldZ(-0.40f), 0.40f), "the magnitude carries through unchanged");

    using namespace cameraunlock::reframework;
    Matrix4x4f world = IdentityCamera();
    const Matrix4x4f axes = world;
    ApplyViewSpacePositionOffset(world, axes, 0.0f, 0.25f, 0.0f);
    Check(Near(world.m[3][1], 0.25f), "a positive pipeline y raises the camera");
}

void TestTheAsymmetricBudgetLandsTheRightWayRound() {
    std::cout << "asymmetric z budget through the boundary:\n";

    const float forward = SteadyLean(-1.0f, false);
    const float backward = SteadyLean(1.0f, false);
    Check(Near(forward, -0.40f), "the processor gives a forward lean the 0.40m budget");
    Check(Near(backward, 0.10f), "the processor gives a backward lean the 0.10m budget");

    const float forwardWorld = AppliedWorldZ(forward);
    const float backwardWorld = AppliedWorldZ(backward);
    Check(forwardWorld > 0.0f && backwardWorld < 0.0f,
          "forward travel is forward in world space and backward is backward");
    Check(Near(forwardWorld, 0.40f) && Near(backwardWorld, -0.10f),
          "the generous budget ends up on the forward side, not the backward one");
    Check(forwardWorld > -backwardWorld * 3.0f,
          "forward travel is the larger of the two");
}

void TestInvertZIsATrackerCorrectionNotAnEngineFlip() {
    std::cout << "invert_z semantics:\n";

    // docs/porting-the-pipeline.md section 11. invert_z exists for a tracker
    // whose z genuinely runs the other way, and it is applied before the clamp
    // precisely so the corrected stream is clamped in the pipeline's own
    // convention. A tracker reporting +z forward with invert_z set must land on
    // exactly the same output as one reporting -z forward with it clear.
    Check(Near(SteadyLean(1.0f, true), SteadyLean(-1.0f, false)),
          "a reversed tracker with invert_z matches a correct tracker without it");
    Check(Near(SteadyLean(1.0f, true), -0.40f),
          "and it still gets the forward budget, because inversion runs before the clamp");
    Check(Near(SteadyLean(-1.0f, true), 0.10f),
          "the reversed tracker's backward lean gets the backward budget");
}

// A camera yawed by `deg` about its up axis and moved to `pos`. Rows stay
// orthonormal, which is all the projection maths requires of them.
cameraunlock::reframework::Matrix4x4f YawedCamera(float deg, float px, float py, float pz) {
    const float r = deg * cameraunlock::reframework::kDegToRad;
    const float c = std::cos(r), s = std::sin(r);
    cameraunlock::reframework::Matrix4x4f m{};
    m.m[0][0] = c;   m.m[0][1] = 0.0f; m.m[0][2] = -s;
    m.m[1][0] = 0.0f; m.m[1][1] = 1.0f; m.m[1][2] = 0.0f;
    m.m[2][0] = s;   m.m[2][1] = 0.0f; m.m[2][2] = c;
    m.m[3][0] = px;  m.m[3][1] = py;   m.m[3][2] = pz;
    m.m[3][3] = 1.0f;
    return m;
}

// Where a world point lands on the GUI canvas for a given camera, computed
// directly from the definition rather than through the helpers under test.
void CanvasPositionOf(const cameraunlock::reframework::Matrix4x4f& cam,
                      float wx, float wy, float wz,
                      float fx, float fy, float& guiX, float& guiY) {
    const float dx = wx - cam.m[3][0];
    const float dy = wy - cam.m[3][1];
    const float dz = wz - cam.m[3][2];
    const float lx = dx * cam.m[0][0] + dy * cam.m[0][1] + dz * cam.m[0][2];
    const float ly = dx * cam.m[1][0] + dy * cam.m[1][1] + dz * cam.m[1][2];
    const float lz = dx * cam.m[2][0] + dy * cam.m[2][1] + dz * cam.m[2][2];
    guiX = -(lx / lz) * fx;
    guiY =  (ly / lz) * fy;
}

// Pixel focal length of a 75 degree vertical FOV on the 1080-tall reference
// canvas: 540 / tan(37.5 deg).
constexpr float kReferenceFocal = 703.9f;

void TestTheLeanTermPinsAMarkerAtItsOwnDepth() {
    std::cout << "world-anchored marker parallax:\n";

    using namespace cameraunlock::reframework;

    const float lean = 0.30f;

    // Pin the delta's own convention first. The ray below negates it, so a sign
    // flip inside ComputeCleanLocalPositionDelta would cancel out there and the
    // projection checks would still pass.
    {
        float delta[3];
        ComputeCleanLocalPositionDelta(IdentityCamera(), YawedCamera(0.0f, lean, 0.0f, 0.0f), delta);
        Check(Near(delta[0], lean) && Near(delta[1], 0.0f) && Near(delta[2], 0.0f),
              "the delta is head minus clean, not the other way round");

        // Ninety degrees of yaw puts the clean camera's row 0 along world -z, so
        // a world +z offset has to come back on the first component, negated.
        // Reading the offset in world axes instead would leave it on the third.
        ComputeCleanLocalPositionDelta(YawedCamera(90.0f, 0.0f, 0.0f, 0.0f),
                                       YawedCamera(90.0f, 0.0f, 0.0f, lean), delta);
        Check(Near(delta[0], -lean) && Near(delta[1], 0.0f) && Near(delta[2], 0.0f),
              "and it is resolved in the clean camera's own axes, not world axes");
    }

    // Pure lateral lean, no head rotation. A marker sitting on the view axis at
    // depth d must move by f * lean / d pixels, and by nothing else.
    for (float depth : { 2.0f, 3.0f, 5.0f }) {
        const Matrix4x4f clean = IdentityCamera();
        const Matrix4x4f head = YawedCamera(0.0f, lean, 0.0f, 0.0f);

        float cleanToHead[3][3];
        ComputeCleanToHeadRotation(clean, head, cleanToHead);
        float delta[3];
        ComputeCleanLocalPositionDelta(clean, head, delta);

        float guiX = 0.0f, guiY = 0.0f;
        Check(ProjectCleanRayToHeadGui(cleanToHead, 0.0f,
                                       -delta[0], -delta[1], depth - delta[2],
                                       kReferenceFocal, kReferenceFocal, guiX, guiY),
              "the leaned ray projects in front of the head camera");
        Check(NearPixels(guiX, kReferenceFocal * lean / depth),
              "lateral lean shifts the marker by f * lean / depth");
        Check(NearPixels(guiY, 0.0f), "and leaves the vertical alone");
    }

    // The same three depths, stated as the numbers a reviewer can check by hand
    // on the 1920x1080 reference canvas.
    {
        const Matrix4x4f clean = IdentityCamera();
        const Matrix4x4f head = YawedCamera(0.0f, lean, 0.0f, 0.0f);
        float cleanToHead[3][3];
        ComputeCleanToHeadRotation(clean, head, cleanToHead);
        float delta[3];
        ComputeCleanLocalPositionDelta(clean, head, delta);

        float at2 = 0.0f, at5 = 0.0f, ignored = 0.0f;
        ProjectCleanRayToHeadGui(cleanToHead, 0.0f, -delta[0], -delta[1], 2.0f - delta[2],
                                 kReferenceFocal, kReferenceFocal, at2, ignored);
        ProjectCleanRayToHeadGui(cleanToHead, 0.0f, -delta[0], -delta[1], 5.0f - delta[2],
                                 kReferenceFocal, kReferenceFocal, at5, ignored);
        Check(at2 > 105.0f && at2 < 107.0f, "a 0.30m lean moves a 2m marker about 106px");
        Check(at5 > 41.0f && at5 < 43.0f, "and a 5m marker about 42px");
        // The term is per-depth, so a caller that dropped the depth would land
        // on one value for both. This is what a container-wide write cannot do.
        Check(!NearPixels(at2, at5), "the shift is depth-dependent, not a single constant");
    }
}

void TestTheLeanTermReproducesTheTrueHeadViewPosition() {
    std::cout << "marker reprojection against the direct projection:\n";

    using namespace cameraunlock::reframework;

    const Matrix4x4f clean = IdentityCamera();
    const Matrix4x4f head = YawedCamera(12.0f, 0.25f, 0.08f, -0.05f);

    float cleanToHead[3][3];
    ComputeCleanToHeadRotation(clean, head, cleanToHead);
    float delta[3];
    ComputeCleanLocalPositionDelta(clean, head, delta);

    // An off-centre world point, so the test exercises more than the view axis.
    const float wx = 0.9f, wy = -0.4f, wz = 3.0f;

    // What the engine draws while the camera transform is the clean one.
    float cleanX = 0.0f, cleanY = 0.0f;
    CanvasPositionOf(clean, wx, wy, wz, kReferenceFocal, kReferenceFocal, cleanX, cleanY);

    // What it should be for the frame that was actually rendered.
    float trueX = 0.0f, trueY = 0.0f;
    CanvasPositionOf(head, wx, wy, wz, kReferenceFocal, kReferenceFocal, trueX, trueY);

    // The anchor read a mod has: the clean canvas position, unprojected to a
    // ray, scaled to the marker's depth, with the lean taken off.
    const float tanRight = -cleanX / kReferenceFocal;
    const float tanUp = cleanY / kReferenceFocal;
    const float depth = wz;  // clean camera is at the origin looking down +z

    float guiX = 0.0f, guiY = 0.0f;
    Check(ProjectCleanRayToHeadGui(cleanToHead, 0.0f,
                                   depth * tanRight - delta[0],
                                   depth * tanUp - delta[1],
                                   depth - delta[2],
                                   kReferenceFocal, kReferenceFocal, guiX, guiY),
          "the depth-scaled leaned ray projects in front of the head camera");
    Check(NearPixels(guiX, trueX), "the reprojected marker lands where the head view puts it (x)");
    Check(NearPixels(guiY, trueY), "the reprojected marker lands where the head view puts it (y)");

    // Rotation alone does not get there. Dropping the lean leaves an error, and
    // that error is the whole of what this term fixes.
    float rotationOnlyX = 0.0f, rotationOnlyY = 0.0f;
    ProjectCleanRayToHeadGui(cleanToHead, 0.0f, depth * tanRight, depth * tanUp, depth,
                             kReferenceFocal, kReferenceFocal, rotationOnlyX, rotationOnlyY);
    Check(!NearPixels(rotationOnlyX, trueX), "rotation-only misses the leaned position");
}

}  // namespace

int RunReMathTests() {
    std::cout << "\n=== RE Engine Math Tests ===\n";
    TestForwardLeanMovesTheCameraForward();
    TestTheAsymmetricBudgetLandsTheRightWayRound();
    TestInvertZIsATrackerCorrectionNotAnEngineFlip();
    TestTheLeanTermPinsAMarkerAtItsOwnDepth();
    TestTheLeanTermReproducesTheTrueHeadViewPosition();
    return g_failures;
}
