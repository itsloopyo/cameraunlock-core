#pragma once

#include <cmath>

namespace cameraunlock::reframework {

constexpr float kDegToRad = 0.0174532925f;

struct Matrix4x4f { float m[4][4]; };
struct alignas(16) REQuat { float x, y, z, w; };

inline REQuat MatrixToQuat(const Matrix4x4f& m) {
    float trace = m.m[0][0] + m.m[1][1] + m.m[2][2];
    REQuat q;
    if (trace > 0.0f) {
        float s = sqrtf(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (m.m[2][1] - m.m[1][2]) / s;
        q.y = (m.m[0][2] - m.m[2][0]) / s;
        q.z = (m.m[1][0] - m.m[0][1]) / s;
    } else if (m.m[0][0] > m.m[1][1] && m.m[0][0] > m.m[2][2]) {
        float s = sqrtf(1.0f + m.m[0][0] - m.m[1][1] - m.m[2][2]) * 2.0f;
        q.w = (m.m[2][1] - m.m[1][2]) / s;
        q.x = 0.25f * s;
        q.y = (m.m[0][1] + m.m[1][0]) / s;
        q.z = (m.m[0][2] + m.m[2][0]) / s;
    } else if (m.m[1][1] > m.m[2][2]) {
        float s = sqrtf(1.0f + m.m[1][1] - m.m[0][0] - m.m[2][2]) * 2.0f;
        q.w = (m.m[0][2] - m.m[2][0]) / s;
        q.x = (m.m[0][1] + m.m[1][0]) / s;
        q.y = 0.25f * s;
        q.z = (m.m[1][2] + m.m[2][1]) / s;
    } else {
        float s = sqrtf(1.0f + m.m[2][2] - m.m[0][0] - m.m[1][1]) * 2.0f;
        q.w = (m.m[1][0] - m.m[0][1]) / s;
        q.x = (m.m[0][2] + m.m[2][0]) / s;
        q.y = (m.m[1][2] + m.m[2][1]) / s;
        q.z = 0.25f * s;
    }
    return q;
}

inline void QuatToMatrix3x3(const REQuat& q, float out[3][3]) {
    float xx=q.x*q.x, yy=q.y*q.y, zz=q.z*q.z;
    float xy=q.x*q.y, xz=q.x*q.z, yz=q.y*q.z;
    float wx=q.w*q.x, wy=q.w*q.y, wz=q.w*q.z;
    out[0][0]=1-2*(yy+zz); out[0][1]=2*(xy+wz);   out[0][2]=2*(xz-wy);
    out[1][0]=2*(xy-wz);   out[1][1]=1-2*(xx+zz);  out[1][2]=2*(yz+wx);
    out[2][0]=2*(xz+wy);   out[2][1]=2*(yz-wx);    out[2][2]=1-2*(xx+yy);
}

inline REQuat QuatMul(const REQuat& a, const REQuat& b) {
    return {
        a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
        a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
        a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
        a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z
    };
}

inline REQuat QuatNorm(const REQuat& q) {
    float lenSq = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
    if (lenSq < 0.00000001f) return {0,0,0,1};
    float inv = 1.0f / sqrtf(lenSq);
    return {q.x*inv, q.y*inv, q.z*inv, q.w*inv};
}

// C = R_head * R_clean^T (3x3). Maps directions from clean camera space
// to head camera space. RE Engine stores basis axes in rows.
inline void ComputeCleanToHeadRotation(const Matrix4x4f& clean, const Matrix4x4f& head, float out[3][3]) {
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            out[i][j] = head.m[i][0] * clean.m[j][0]
                      + head.m[i][1] * clean.m[j][1]
                      + head.m[i][2] * clean.m[j][2];
        }
    }
}

// Left-multiply the upper-left 3x3 of a world matrix (its rotation basis) by
// rot, column by column. The translation row (m[3]) is untouched.
inline void PreMultiplyRotation3x3(Matrix4x4f& worldMat, const float rot[3][3]) {
    for (int c = 0; c < 3; c++) {
        float c0 = worldMat.m[0][c];
        float c1 = worldMat.m[1][c];
        float c2 = worldMat.m[2][c];
        worldMat.m[0][c] = rot[0][0]*c0 + rot[0][1]*c1 + rot[0][2]*c2;
        worldMat.m[1][c] = rot[1][0]*c0 + rot[1][1]*c1 + rot[1][2]*c2;
        worldMat.m[2][c] = rot[2][0]*c0 + rot[2][1]*c1 + rot[2][2]*c2;
    }
}

// Horizon-locked yaw plus camera-local pitch/roll. Prevents leaning artifacts
// at extreme angles by rotating the world-space basis about the vertical axis.
// Angles are radians, pre-signed by the caller.
inline void ApplyWorldSpaceHeadRotation(Matrix4x4f& worldMat, float yawRad, float pitchRad, float rollRad) {
    float cy = cosf(yawRad), sy = -sinf(yawRad);
    for (int r = 0; r < 3; r++) {
        float x = worldMat.m[r][0];
        float z = worldMat.m[r][2];
        worldMat.m[r][0] = x * cy - z * sy;
        worldMat.m[r][2] = x * sy + z * cy;
    }

    float cp = cosf(pitchRad), sp = sinf(pitchRad);
    float cr = cosf(rollRad), sr = sinf(rollRad);
    float prRot[3][3] = {
        { cr,      sr,      0   },
        {-cp*sr,   cp*cr,   sp  },
        { sp*sr,  -sp*cr,   cp  }
    };

    PreMultiplyRotation3x3(worldMat, prRot);
}

// Camera-local YPR composed as a shortest-arc quaternion (gimbal-lock-free).
// Angles are radians, pre-signed by the caller.
inline void ApplyCameraLocalHeadRotation(Matrix4x4f& worldMat, float yawRad, float pitchRad, float rollRad) {
    float hy = yawRad * 0.5f, hp = pitchRad * 0.5f, hr = rollRad * 0.5f;
    REQuat qy = {0, sinf(hy), 0, cosf(hy)};
    REQuat qx = {sinf(hp), 0, 0, cosf(hp)};
    REQuat qz = {0, 0, sinf(hr), cosf(hr)};
    REQuat q = QuatNorm(QuatMul(QuatMul(qy, qx), qz));

    float headRot[3][3];
    QuatToMatrix3x3(q, headRot);

    PreMultiplyRotation3x3(worldMat, headRot);
}

// Translate the camera in the body-oriented basis captured before head
// rotation, so the offset follows body orientation rather than the head-turned
// view. Offsets are in meters, in the pipeline's own convention.
//
// This is the engine boundary, and the boundary is where the z flip belongs.
// Negative z is the forward lean everywhere inside the pipeline and the
// asymmetric clamp is built on it - [-limit_z, +limit_z_back] puts the generous
// 0.40m budget on the negative side. RE Engine stores its basis axes in rows and
// row 2 is camera FORWARD (ProjectAimToViewTangents walks the aim point out
// along clean.m[2]), so pz multiplies a forward axis and the incoming z has to
// be negated to reach it. Without that negation a forward lean drove the camera
// backwards on the 0.40m budget and a backward lean forwards on 0.10m.
//
// X is negated here as well, to match RE Engine handedness. Doing either flip
// with the invert_x / invert_z config flags instead is the documented mistake:
// those land BEFORE the processor's clamp, so the lean comes out on the wrong
// budget. See docs/porting-the-pipeline.md section 11.
inline void ApplyViewSpacePositionOffset(Matrix4x4f& worldMat, const Matrix4x4f& preRotationAxes,
                                         float offsetX, float offsetY, float offsetZ) {
    float px = -offsetX;
    float py = offsetY;
    float pz = -offsetZ;
    const Matrix4x4f& gm = preRotationAxes;
    worldMat.m[3][0] += px * gm.m[0][0] + py * gm.m[1][0] + pz * gm.m[2][0];
    worldMat.m[3][1] += px * gm.m[0][1] + py * gm.m[1][1] + pz * gm.m[2][1];
    worldMat.m[3][2] += px * gm.m[0][2] + py * gm.m[1][2] + pz * gm.m[2][2];
}

// Express the head-tracked camera's translation relative to the clean camera
// in the clean camera's local axes.
//
// World-anchored GUI compensation SUBTRACTS this from a depth-scaled anchor ray
// before projecting it, which is what pins a marker under 6DOF head translation:
// an anchor at depth d along the clean-view ray (tanRight, tanUp, 1) is
// (d*tanRight, d*tanUp, d) in clean-local axes, and the same point in head-local
// axes is that minus this delta, rotated by ComputeCleanToHeadRotation. The
// depth is what makes the term per-marker - the screen shift is lean/depth, so a
// caller with one write for markers at several depths has no single right value
// and must leave the term out.
inline void ComputeCleanLocalPositionDelta(const Matrix4x4f& clean, const Matrix4x4f& head, float out[3]) {
    float dwx = head.m[3][0] - clean.m[3][0];
    float dwy = head.m[3][1] - clean.m[3][1];
    float dwz = head.m[3][2] - clean.m[3][2];
    for (int i = 0; i < 3; i++) {
        out[i] = dwx * clean.m[i][0] + dwy * clean.m[i][1] + dwz * clean.m[i][2];
    }
}

// Project a ray expressed in clean-camera-local space into head-view GUI pixel
// offsets, applying the clean-to-head rotation (from ComputeCleanToHeadRotation)
// with a roll correction in direction space. Rotating the X/Y rows of the
// rotation by roll keeps the projection upright when head roll combines with
// yaw/pitch; rotating in screen space instead would distort with FOV/aspect.
// fx/fy are pixel focal lengths. Returns false when the ray lands behind the
// head-tracked camera.
inline bool ProjectCleanRayToHeadGui(const float cleanToHead[3][3], float rollRad,
                                     float cleanX, float cleanY, float cleanZ,
                                     float fx, float fy, float& guiX, float& guiY) {
    float cr = cosf(rollRad), sr = sinf(rollRad);
    float C0[3], C1[3];
    for (int j = 0; j < 3; j++) {
        C0[j] = cr * cleanToHead[0][j] - sr * cleanToHead[1][j];
        C1[j] = sr * cleanToHead[0][j] + cr * cleanToHead[1][j];
    }
    float vx = C0[0]*cleanX + C0[1]*cleanY + C0[2]*cleanZ;
    float vy = C1[0]*cleanX + C1[1]*cleanY + C1[2]*cleanZ;
    float vz = cleanToHead[2][0]*cleanX + cleanToHead[2][1]*cleanY + cleanToHead[2][2]*cleanZ;
    if (vz < 1e-4f) return false;
    guiX = -(vx / vz) * fx;
    guiY =  (vy / vz) * fy;
    return true;
}

// Project the clean camera's forward direction through the head-tracked
// rotation basis, ignoring translation entirely. Yields the rotation half of
// the clean-view-to-head-view difference, as screen tangents.
//
// That difference has a translation half too. The post-render hook restores the
// clean camera in full, position row included, so the frame is drawn from the
// leaned eye while the GUI projects its world anchors from the un-leaned one,
// and the leftover is lean/depth. These tangents carry none of it: they are
// depth-independent, and a caller that shifts markers sitting at several depths
// with one write cannot express a per-depth value anyway. A caller holding one
// marker at a known depth adds the translation half itself, by subtracting
// ComputeCleanLocalPositionDelta from a depth-scaled ray and projecting that
// through ProjectCleanRayToHeadGui.
//
// Returns false when the clean forward direction lands behind the head-tracked
// view.
inline bool ProjectForwardToViewTangents(const Matrix4x4f& clean, const Matrix4x4f& head,
                                         float& tanRight, float& tanUp) {
    float dx = clean.m[2][0];
    float dy = clean.m[2][1];
    float dz = clean.m[2][2];

    float vx = dx * head.m[0][0] + dy * head.m[0][1] + dz * head.m[0][2];
    float vy = dx * head.m[1][0] + dy * head.m[1][1] + dz * head.m[1][2];
    float vz = dx * head.m[2][0] + dy * head.m[2][1] + dz * head.m[2][2];

    if (vz <= 1e-4f) return false;

    tanRight = vx / vz;
    tanUp = vy / vz;
    return true;
}

// Project the clean aim point (camera forward * aimDist) into the head-tracked
// view to get the screen-space tangents GUI/crosshair compensation reads.
// Returns false when the aim point falls behind the head-tracked camera.
inline bool ProjectAimToViewTangents(const Matrix4x4f& clean, const Matrix4x4f& head,
                                     float aimDist, float& tanRight, float& tanUp) {
    float aimPtX = clean.m[3][0] + aimDist * clean.m[2][0];
    float aimPtY = clean.m[3][1] + aimDist * clean.m[2][1];
    float aimPtZ = clean.m[3][2] + aimDist * clean.m[2][2];

    float dx = aimPtX - head.m[3][0];
    float dy = aimPtY - head.m[3][1];
    float dz = aimPtZ - head.m[3][2];

    float vx = dx * head.m[0][0] + dy * head.m[0][1] + dz * head.m[0][2];
    float vy = dx * head.m[1][0] + dy * head.m[1][1] + dz * head.m[1][2];
    float vz = dx * head.m[2][0] + dy * head.m[2][1] + dz * head.m[2][2];

    if (vz <= 1e-4f) return false;

    tanRight = vx / vz;
    tanUp = vy / vz;
    return true;
}

} // namespace cameraunlock::reframework
