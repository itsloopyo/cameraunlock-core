// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>

// The held weapon, for an engine that keeps its view as a world-to-eye matrix,
// draws the weapon at a FOV of its own and reads the point rounds leave from
// back off the drawn weapon (Cube 2 and Tesseract's calcavatarpos).
//
// A matrix is 16 numbers laid out so that the eye-space point of world point
// (x, y, z) is x * m[0..3] + y * m[4..7] + z * m[8..11] + m[12..15]: OpenGL's
// column-major matrix, or Direct3D's row-major one for row vectors. Eye x and y
// run across the view, eye z along it, either sign.
namespace cameraunlock::camera::viewmodel_matrix {

using Mat = std::array<double, 16>;
using Vec = std::array<double, 3>;

/// a after b: (a * b)(p) = a(b(p)).
inline Mat Mul(const Mat& a, const Mat& b) {
    Mat out{};
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) {
            double v = 0;
            for (int k = 0; k < 4; ++k) v += a[k * 4 + r] * b[c * 4 + k];
            out[c * 4 + r] = v;
        }
    return out;
}

/// Gauss-Jordan with partial pivoting. A view matrix always has an inverse; a
/// matrix with none is an engine handing over something else, and throws.
inline Mat Inverse(const Mat& m) {
    double a[4][8];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            a[r][c] = m[c * 4 + r];
            a[r][c + 4] = r == c ? 1.0 : 0.0;
        }
    for (int c = 0; c < 4; ++c) {
        int pivot = c;
        for (int r = c + 1; r < 4; ++r)
            if (std::fabs(a[r][c]) > std::fabs(a[pivot][c])) pivot = r;
        if (std::fabs(a[pivot][c]) < 1e-12) throw std::runtime_error("a view matrix with no inverse");
        if (pivot != c)
            for (int k = 0; k < 8; ++k) std::swap(a[c][k], a[pivot][k]);
        const double inv = 1.0 / a[c][c];
        for (int k = 0; k < 8; ++k) a[c][k] *= inv;
        for (int r = 0; r < 4; ++r) {
            if (r == c || a[r][c] == 0.0) continue;
            const double f = a[r][c];
            for (int k = 0; k < 8; ++k) a[r][k] -= f * a[c][k];
        }
    }
    Mat out{};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) out[c * 4 + r] = a[r][c + 4];
    return out;
}

inline Vec Apply(const Mat& m, const Vec& p) {
    Vec out{};
    for (int r = 0; r < 3; ++r) out[r] = m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r];
    return out;
}

/// Where the view's eye is in the world.
inline Vec Eye(const Mat& view) {
    const Mat inv = Inverse(view);
    return {inv[12], inv[13], inv[14]};
}

/// The view the weapon's own pass is drawn through. The engine draws the weapon
/// through the world's view with a projection of its own, so the clean view
/// puts it where the world's projection would put a point `k` times further
/// across the view (k = tan(world FOV / 2) / tan(weapon FOV / 2), both on one
/// axis). The weapon's view is
///
///   V = S^-1 * D' * C^-1 * S * C,   S = diag(k, k, 1, 1)
///
/// for the clean view C and the drawn view D moved to the weapon's eye (D'):
/// each point of the weapon lands on the pixel where the world, drawn through
/// D', shows the point the clean view showed it at. With D = C it is C, so the
/// weapon keeps its stock size and shape and stays where the clean aim holds
/// it through a head turn. `eyeShare` says where its eye is: 1 the drawn eye,
/// 0 the clean one, between on the line joining them.
inline Mat WeaponView(const Mat& clean, const Mat& drawn, double k, double eyeShare) {
    Mat d = drawn;
    if (eyeShare != 1.0) {
        const Vec ec = Eye(clean), ed = Eye(drawn);
        // From an eye `back` short of the drawn one, world point p shows where
        // the drawn view shows p + back.
        Vec back{};
        for (int i = 0; i < 3; ++i) back[i] = (1.0 - eyeShare) * (ed[i] - ec[i]);
        for (int r = 0; r < 3; ++r) d[12 + r] += d[r] * back[0] + d[4 + r] * back[1] + d[8 + r] * back[2];
    }
    Mat s{}, sInv{};
    s[0] = s[5] = k;
    sInv[0] = sInv[5] = 1.0 / k;
    s[10] = s[15] = sInv[10] = sInv[15] = 1.0;
    return Mul(sInv, Mul(d, Mul(Inverse(clean), Mul(s, clean))));
}

/// The inverse view-projection the clean view would have had, from the drawn
/// view's (`inverse` = (P * drawn)^-1): (P * clean)^-1 = clean^-1 * drawn *
/// (P * drawn)^-1. Whatever the projection holds (a jitter) is kept.
inline Mat CleanInverse(const Mat& clean, const Mat& drawn, const Mat& inverse) {
    return Mul(Inverse(clean), Mul(drawn, inverse));
}

/// The weapon eye share that puts the drawn muzzle on the pixel of the point
/// rounds leave from, whatever the lean, in a pass drawn at the FOV the engine
/// reads back through. The engine reads that point back off the weapon as the
/// clean eye plus a fixed reach along the ray through the muzzle's pixel; the
/// weapon's pass draws the muzzle on that same ray at the muzzle's k-scaled
/// distance, which can be nearer or further. Seen from the clean eye moved by
/// `that distance / reach` of the lean, the drawn muzzle and the read-back
/// point line up from the drawn eye. `raw` is the muzzle as the model placed it
/// and `readBack` what the engine made of it, both in the world. Nothing when
/// the engine read nothing back.
inline std::optional<double> MuzzleShare(const Mat& clean, const Vec& raw, const Vec& readBack) {
    if (raw == readBack) return std::nullopt;
    // In the clean eye's axes the eye is the origin, and the drawn muzzle is
    // where the read-back ray reaches the muzzle's depth.
    return Apply(clean, raw)[2] / Apply(clean, readBack)[2];
}

}  // namespace cameraunlock::camera::viewmodel_matrix
