// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <cmath>

namespace cameraunlock::rendering {

/// The axes to draw a held weapon from when the engine draws it in a pass of its
/// own at a narrower FOV than the world's. The weapon points along the `clean`
/// view's forward; drawn from the `drawn` view at its own FOV it would swing out
/// by the ratio of the half-angle tangents. `out` is `drawn` turned by the
/// shortest arc that puts the clean forward where the world's projection puts it:
/// `tanRatio` = tan(world FOV / 2) / tan(weapon FOV / 2).
///
/// Each set is three rows of four floats in one world frame: forward first, then
/// right and up (or the two lateral axes in either order). The fourth float of
/// each row is copied from `drawn`, and each turned row keeps `drawn`'s length.
/// With the clean forward behind the drawn view, a ratio that is not positive, or
/// no turn between them, `out` is `drawn`.
inline void WeaponPassAxes(const float (*clean)[4], const float (*drawn)[4], double tanRatio, float (*out)[4]) {
    const auto unit = [](const float* row, double v[3]) {
        const double n = std::sqrt(double(row[0]) * row[0] + double(row[1]) * row[1] + double(row[2]) * row[2]);
        for (int i = 0; i < 3; ++i) v[i] = row[i] / n;
    };
    double d[3][3], aim[3];
    for (int i = 0; i < 3; ++i) unit(drawn[i], d[i]);
    unit(clean[0], aim);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 4; ++j) out[i][j] = drawn[i][j];
    // The aim in the drawn view's axes, and where it must sit in the weapon's.
    const double a[3] = {aim[0] * d[0][0] + aim[1] * d[0][1] + aim[2] * d[0][2],
                         aim[0] * d[1][0] + aim[1] * d[1][1] + aim[2] * d[1][2],
                         aim[0] * d[2][0] + aim[1] * d[2][1] + aim[2] * d[2][2]};
    if (!(a[0] > 1e-6) || !(tanRatio > 0)) return;
    double b[3] = {a[0], a[1] / tanRatio, a[2] / tanRatio};
    const double bn = std::sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
    for (double& v : b) v /= bn;
    // The turn taking b onto a (Rodrigues), applied to the view's own axes.
    double k[3] = {b[1] * a[2] - b[2] * a[1], b[2] * a[0] - b[0] * a[2], b[0] * a[1] - b[1] * a[0]};
    const double an = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    const double s = std::sqrt(k[0] * k[0] + k[1] * k[1] + k[2] * k[2]) / an;
    if (s < 1e-12) return;
    const double c = (a[0] * b[0] + a[1] * b[1] + a[2] * b[2]) / an;
    const double kn = s * an;
    for (double& v : k) v /= kn;
    for (int i = 0; i < 3; ++i) {
        const double e[3] = {i == 0 ? 1.0 : 0.0, i == 1 ? 1.0 : 0.0, i == 2 ? 1.0 : 0.0};
        const double kd = (k[0] * e[0] + k[1] * e[1] + k[2] * e[2]) * (1 - c);
        const double ke[3] = {k[1] * e[2] - k[2] * e[1], k[2] * e[0] - k[0] * e[2], k[0] * e[1] - k[1] * e[0]};
        const double v[3] = {e[0] * c + ke[0] * s + k[0] * kd, e[1] * c + ke[1] * s + k[1] * kd,
                             e[2] * c + ke[2] * s + k[2] * kd};
        // v is the new axis in the drawn view's axes; back to the world, at the drawn row's length.
        const double len = std::sqrt(double(drawn[i][0]) * drawn[i][0] + double(drawn[i][1]) * drawn[i][1] +
                                     double(drawn[i][2]) * drawn[i][2]);
        for (int j = 0; j < 3; ++j)
            out[i][j] = static_cast<float>((v[0] * d[0][j] + v[1] * d[1][j] + v[2] * d[2][j]) * len);
    }
}

}  // namespace cameraunlock::rendering
