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

} // namespace cameraunlock::reframework
