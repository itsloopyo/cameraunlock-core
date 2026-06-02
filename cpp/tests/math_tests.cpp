// Characterization tests for the RE Engine head-tracking math in
// reframework/re_math.h. These lock the transforms applied to the game's
// world-to-camera matrix: a transcription error (or any future "cleanup" of
// the arithmetic) breaks the core look-vs-aim behaviour in a way no in-game
// playtest would catch automatically.

#include <cameraunlock/reframework/re_math.h>
#include <cameraunlock/rendering/gui_marker_compensation.h>

#include <cmath>
#include <iostream>

namespace {

using namespace cameraunlock::reframework;

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

Matrix4x4f Identity() {
    Matrix4x4f m{};
    m.m[0][0] = m.m[1][1] = m.m[2][2] = m.m[3][3] = 1.0f;
    return m;
}

bool MatrixBitEqual(const Matrix4x4f& a, const Matrix4x4f& b) {
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            if (a.m[i][j] != b.m[i][j]) return false;
    return true;
}

// Rows of the 3x3 block must stay orthonormal after any rigid rotation.
bool RotationIsOrthonormal(const Matrix4x4f& m) {
    for (int r = 0; r < 3; ++r) {
        float len = std::sqrt(m.m[r][0]*m.m[r][0] + m.m[r][1]*m.m[r][1] + m.m[r][2]*m.m[r][2]);
        if (!NearEqual(len, 1.0f, 1e-3f)) return false;
    }
    auto dot = [&](int a, int b) {
        return m.m[a][0]*m.m[b][0] + m.m[a][1]*m.m[b][1] + m.m[a][2]*m.m[b][2];
    };
    return NearEqual(dot(0,1), 0.f, 1e-3f) && NearEqual(dot(0,2), 0.f, 1e-3f) && NearEqual(dot(1,2), 0.f, 1e-3f);
}

const float kHalfPi = 1.57079632679f;

}  // namespace

int RunMathTests() {
    std::cout << "RE Engine head-tracking math tests\n";

    // Quaternion primitives.
    {
        REQuat n = QuatNorm(REQuat{0, 0, 0, 2});
        Check(NearEqual(n.x, 0) && NearEqual(n.y, 0) && NearEqual(n.z, 0) && NearEqual(n.w, 1),
              "QuatNorm scales to unit length");

        REQuat degenerate = QuatNorm(REQuat{0, 0, 0, 0});
        Check(NearEqual(degenerate.w, 1) && NearEqual(degenerate.x, 0),
              "QuatNorm of near-zero returns identity");

        REQuat id{0, 0, 0, 1};
        REQuat prod = QuatMul(id, REQuat{0.1f, 0.2f, 0.3f, 0.9f});
        Check(NearEqual(prod.x, 0.1f) && NearEqual(prod.y, 0.2f) && NearEqual(prod.z, 0.3f) && NearEqual(prod.w, 0.9f),
              "QuatMul by identity is a no-op");

        float r[3][3];
        QuatToMatrix3x3(id, r);
        Check(NearEqual(r[0][0], 1) && NearEqual(r[1][1], 1) && NearEqual(r[2][2], 1) &&
              NearEqual(r[0][1], 0) && NearEqual(r[2][0], 0),
              "QuatToMatrix3x3 of identity quat is identity");

        REQuat q = MatrixToQuat(Identity());
        Check(NearEqual(q.x, 0) && NearEqual(q.y, 0) && NearEqual(q.z, 0) && NearEqual(q.w, 1),
              "MatrixToQuat of identity is identity quat");
    }

    // Zero rotation must not perturb the matrix in either yaw mode.
    {
        Matrix4x4f m = Identity();
        ApplyWorldSpaceHeadRotation(m, 0, 0, 0);
        Check(NearEqual(m.m[0][0], 1) && NearEqual(m.m[1][1], 1) && NearEqual(m.m[2][2], 1) &&
              NearEqual(m.m[0][2], 0) && NearEqual(m.m[2][0], 0),
              "world-space zero rotation leaves identity unchanged");

        Matrix4x4f m2 = Identity();
        ApplyCameraLocalHeadRotation(m2, 0, 0, 0);
        Check(NearEqual(m2.m[0][0], 1) && NearEqual(m2.m[1][1], 1) && NearEqual(m2.m[2][2], 1) &&
              NearEqual(m2.m[0][2], 0) && NearEqual(m2.m[2][0], 0),
              "camera-local zero rotation leaves identity unchanged");
    }

    // Zero rotation must be a BYTE-identical no-op on an arbitrary world
    // matrix, not merely near-equal on the identity. The RE Engine mods skip
    // the rotation block entirely when yaw/pitch/roll are exactly zero
    // (position-only mode, perfectly centered view); that skip is only
    // legitimate if applying the zero rotation would not have changed a
    // single bit of the matrix.
    {
        const Matrix4x4f sample = {{
            { 0.36f, -0.48f,  0.80f, 0.0f},
            { 0.80f,  0.60f,  0.00f, 0.0f},
            {-0.48f,  0.64f,  0.60f, 0.0f},
            { 1.50f,  2.25f, -3.75f, 1.0f},
        }};

        Matrix4x4f m = sample;
        ApplyWorldSpaceHeadRotation(m, 0, 0, 0);
        Check(MatrixBitEqual(m, sample),
              "world-space zero rotation is byte-identical on arbitrary matrix");

        Matrix4x4f m2 = sample;
        ApplyCameraLocalHeadRotation(m2, 0, 0, 0);
        Check(MatrixBitEqual(m2, sample),
              "camera-local zero rotation is byte-identical on arbitrary matrix");

        Matrix4x4f m3 = sample;
        ApplyViewSpacePositionOffset(m3, sample, 0, 0, 0);
        Check(MatrixBitEqual(m3, sample),
              "zero position offset is byte-identical on arbitrary matrix");
    }

    // 90-degree pure yaw produces the same known basis in both modes (pitch =
    // roll = 0): m[0][2] = -1, m[2][0] = +1, m[1][1] = 1.
    {
        Matrix4x4f m = Identity();
        ApplyWorldSpaceHeadRotation(m, kHalfPi, 0, 0);
        Check(NearEqual(m.m[0][2], -1.f) && NearEqual(m.m[2][0], 1.f) && NearEqual(m.m[1][1], 1.f),
              "world-space 90deg yaw maps to expected basis");
        Check(RotationIsOrthonormal(m), "world-space 90deg yaw stays orthonormal");

        Matrix4x4f m2 = Identity();
        ApplyCameraLocalHeadRotation(m2, kHalfPi, 0, 0);
        Check(NearEqual(m2.m[0][2], -1.f) && NearEqual(m2.m[2][0], 1.f) && NearEqual(m2.m[1][1], 1.f),
              "camera-local 90deg yaw maps to expected basis");
        Check(RotationIsOrthonormal(m2), "camera-local 90deg yaw stays orthonormal");
    }

    // Combined yaw+pitch+roll keeps the basis orthonormal (no shear/scale leak).
    {
        Matrix4x4f m = Identity();
        ApplyCameraLocalHeadRotation(m, 0.3f, -0.4f, 0.2f);
        Check(RotationIsOrthonormal(m), "camera-local combined rotation stays orthonormal");

        Matrix4x4f m2 = Identity();
        ApplyWorldSpaceHeadRotation(m2, 0.3f, -0.4f, 0.2f);
        Check(RotationIsOrthonormal(m2), "world-space combined rotation stays orthonormal");
    }

    // Position offset translates along the pre-rotation basis; X is inverted.
    {
        Matrix4x4f m = Identity();
        ApplyViewSpacePositionOffset(m, Identity(), 0.5f, 0.2f, -0.3f);
        Check(NearEqual(m.m[3][0], -0.5f) && NearEqual(m.m[3][1], 0.2f) && NearEqual(m.m[3][2], -0.3f),
              "position offset applies inverted X along identity basis");

        Matrix4x4f none = Identity();
        ApplyViewSpacePositionOffset(none, Identity(), 0, 0, 0);
        Check(NearEqual(none.m[3][0], 0) && NearEqual(none.m[3][1], 0) && NearEqual(none.m[3][2], 0),
              "zero position offset leaves translation unchanged");
    }

    // Aim-tangent projection: aligned clean/head views put the reticle at center;
    // a degenerate (behind-camera) aim point reports failure.
    {
        float tanR = 9.f, tanU = 9.f;
        bool ok = ProjectAimToViewTangents(Identity(), Identity(), 50.f, tanR, tanU);
        Check(ok && NearEqual(tanR, 0.f) && NearEqual(tanU, 0.f),
              "aligned views project aim to screen center");

        float tr = 0.f, tu = 0.f;
        bool behind = ProjectAimToViewTangents(Identity(), Identity(), 0.f, tr, tu);
        Check(!behind, "degenerate aim point (zero depth) reports failure");
    }

    // Rotation-only forward projection: identical bases give zero tangents even
    // under head translation; a yawed head view shifts the tangent; a head view
    // facing away from the clean forward reports failure.
    {
        Matrix4x4f clean = Identity();
        Matrix4x4f head = Identity();
        head.m[3][0] = 5.f; head.m[3][1] = -2.f; head.m[3][2] = 1.f;

        float tanR = 9.f, tanU = 9.f;
        bool ok = ProjectForwardToViewTangents(clean, head, tanR, tanU);
        Check(ok && NearEqual(tanR, 0.f) && NearEqual(tanU, 0.f),
              "forward projection ignores head translation");

        Matrix4x4f headYawed = Identity();
        ApplyCameraLocalHeadRotation(headYawed, 0.2f, 0.f, 0.f);
        ok = ProjectForwardToViewTangents(clean, headYawed, tanR, tanU);
        Check(ok && !NearEqual(tanR, 0.f, 1e-4f) && NearEqual(tanU, 0.f),
              "yawed head view shifts horizontal tangent only");

        Matrix4x4f headFlipped = Identity();
        ApplyCameraLocalHeadRotation(headFlipped, kHalfPi * 2.f, 0.f, 0.f);
        ok = ProjectForwardToViewTangents(clean, headFlipped, tanR, tanU);
        Check(!ok, "head view facing away reports failure");
    }

    // GUI focal-length helpers: projection-diagonal path and FOV fallback must
    // agree with each other for a matching FOV, and both reject degenerate input.
    {
        constexpr float kHalfW = 960.f, kHalfH = 540.f;

        float fx = 0.f, fy = 0.f;
        Check(cameraunlock::rendering::FocalLengthsFromProjection(1.0f, 1.7778f, kHalfW, kHalfH, fx, fy) &&
              NearEqual(fx, 960.f) && NearEqual(fy, 960.0112f, 0.01f),
              "projection diagonal terms scale to pixel focal lengths");

        Check(!cameraunlock::rendering::FocalLengthsFromProjection(0.f, 1.f, kHalfW, kHalfH, fx, fy),
              "degenerate projection term rejected");

        // 90-degree vertical FOV: fy = halfH / tan(45deg) = halfH; fx = fy at the
        // canvas aspect because fx = halfW / (tan * aspect) and aspect = halfW/halfH.
        float ffx = 0.f, ffy = 0.f;
        Check(cameraunlock::rendering::FocalLengthsFromVerticalFov(90.f, kHalfW, kHalfH, ffx, ffy) &&
              NearEqual(ffy, kHalfH, 0.05f) && NearEqual(ffx, ffy, 0.05f),
              "90deg vertical FOV maps to half-canvas focal length");

        Check(!cameraunlock::rendering::FocalLengthsFromVerticalFov(5.f, kHalfW, kHalfH, ffx, ffy),
              "implausible FOV rejected");
    }

    // Clean-local position delta: head translation expressed in the clean
    // camera's local axes.
    {
        Matrix4x4f clean = Identity();
        Matrix4x4f head = Identity();
        head.m[3][0] = 1.f; head.m[3][1] = 2.f; head.m[3][2] = 3.f;

        float delta[3] = {};
        ComputeCleanLocalPositionDelta(clean, head, delta);
        Check(NearEqual(delta[0], 1.f) && NearEqual(delta[1], 2.f) && NearEqual(delta[2], 3.f),
              "identity clean basis passes world delta through unchanged");

        // Clean camera yawed 90deg about the vertical axis: its local X axis is
        // world -Z and local Z is world +X, so a world +X delta lands on local Z.
        Matrix4x4f cleanYawed = Identity();
        ApplyWorldSpaceHeadRotation(cleanYawed, kHalfPi, 0, 0);
        Matrix4x4f head2 = cleanYawed;
        head2.m[3][0] = 1.f;
        float delta2[3] = {};
        ComputeCleanLocalPositionDelta(cleanYawed, head2, delta2);
        Check(RotationIsOrthonormal(cleanYawed) &&
              NearEqual(delta2[0]*delta2[0] + delta2[1]*delta2[1] + delta2[2]*delta2[2], 1.f, 1e-3f),
              "rotated clean basis preserves delta magnitude");
    }

    // Clean-ray-to-head-GUI projection: roll-aware reprojection of a
    // clean-camera-local ray through the clean-to-head rotation.
    {
        const float kIdentity3x3[3][3] = { {1,0,0}, {0,1,0}, {0,0,1} };
        constexpr float kFx = 960.f, kFy = 960.f;

        float gx = 9.f, gy = 9.f;
        bool ok = ProjectCleanRayToHeadGui(kIdentity3x3, 0.f, 0.f, 0.f, 1.f, kFx, kFy, gx, gy);
        Check(ok && NearEqual(gx, 0.f) && NearEqual(gy, 0.f),
              "centered ray through identity rotation projects to GUI origin");

        // A ray offset to camera-right maps to a negative GUI X (RE Engine GUI
        // X grows leftward relative to view space).
        ok = ProjectCleanRayToHeadGui(kIdentity3x3, 0.f, 0.1f, 0.f, 1.f, kFx, kFy, gx, gy);
        Check(ok && gx < -0.1f && NearEqual(gy, 0.f),
              "rightward ray maps to negative GUI X");

        // Pure roll on a centered ray must not move it off origin.
        ok = ProjectCleanRayToHeadGui(kIdentity3x3, 1.0f, 0.f, 0.f, 1.f, kFx, kFy, gx, gy);
        Check(ok && NearEqual(gx, 0.f) && NearEqual(gy, 0.f),
              "pure roll leaves centered ray at GUI origin");

        // A ray behind the head camera reports failure.
        ok = ProjectCleanRayToHeadGui(kIdentity3x3, 0.f, 0.f, 0.f, -1.f, kFx, kFy, gx, gy);
        Check(!ok, "behind-camera ray reports failure");
    }

    if (g_failures == 0) {
        std::cout << "Math tests: all passed\n";
    } else {
        std::cout << "Math tests: " << g_failures << " failure(s)\n";
    }
    return g_failures;
}
