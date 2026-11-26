#pragma once

#include <cmath>

namespace cameraunlock::math {

// Rodrigues' rotation formula: rotate vector v around unit axis k by angle
// This is used for applying head tracking rotations to camera vectors

// Rotate vector v around axis k using precomputed cos/sin values
// v: input vector [3]
// k: unit rotation axis [3]
// cosAngle: cos(angle)
// sinAngle: sin(angle)
// out: output vector [3]
inline void RotateAroundAxis(const float v[3], const float k[3],
                             float cosAngle, float sinAngle, float out[3]) {
    // Rodrigues' formula: v_rot = v*cos(θ) + (k×v)*sin(θ) + k*(k·v)*(1-cos(θ))

    // Cross product: k × v
    float crossX = k[1] * v[2] - k[2] * v[1];
    float crossY = k[2] * v[0] - k[0] * v[2];
    float crossZ = k[0] * v[1] - k[1] * v[0];

    // Dot product: k · v
    float dot = k[0] * v[0] + k[1] * v[1] + k[2] * v[2];

    // 1 - cos(θ)
    float omc = 1.0f - cosAngle;

    out[0] = v[0] * cosAngle + crossX * sinAngle + k[0] * dot * omc;
    out[1] = v[1] * cosAngle + crossY * sinAngle + k[1] * dot * omc;
    out[2] = v[2] * cosAngle + crossZ * sinAngle + k[2] * dot * omc;
}

// Rotate vector v around axis k by angle in radians
inline void RotateAroundAxis(const float v[3], const float k[3],
                             float angleRad, float out[3]) {
    RotateAroundAxis(v, k, std::cos(angleRad), std::sin(angleRad), out);
}

// Double precision versions
inline void RotateAroundAxis(const double v[3], const double k[3],
                             double cosAngle, double sinAngle, double out[3]) {
    double crossX = k[1] * v[2] - k[2] * v[1];
    double crossY = k[2] * v[0] - k[0] * v[2];
    double crossZ = k[0] * v[1] - k[1] * v[0];
    double dot = k[0] * v[0] + k[1] * v[1] + k[2] * v[2];
    double omc = 1.0 - cosAngle;

    out[0] = v[0] * cosAngle + crossX * sinAngle + k[0] * dot * omc;
    out[1] = v[1] * cosAngle + crossY * sinAngle + k[1] * dot * omc;
    out[2] = v[2] * cosAngle + crossZ * sinAngle + k[2] * dot * omc;
}

inline void RotateAroundAxis(const double v[3], const double k[3],
                             double angleRad, double out[3]) {
    RotateAroundAxis(v, k, std::cos(angleRad), std::sin(angleRad), out);
}

// Normalize a 3D vector in place, returns length
inline float Normalize3(float v[3]) {
    float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len > 0.0001f) {
        v[0] /= len;
        v[1] /= len;
        v[2] /= len;
    }
    return len;
}

inline double Normalize3(double v[3]) {
    double len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len > 0.0001) {
        v[0] /= len;
        v[1] /= len;
        v[2] /= len;
    }
    return len;
}

// Cross product: result = a × b
inline void Cross3(const float a[3], const float b[3], float result[3]) {
    result[0] = a[1] * b[2] - a[2] * b[1];
    result[1] = a[2] * b[0] - a[0] * b[2];
    result[2] = a[0] * b[1] - a[1] * b[0];
}

inline void Cross3(const double a[3], const double b[3], double result[3]) {
    result[0] = a[1] * b[2] - a[2] * b[1];
    result[1] = a[2] * b[0] - a[0] * b[2];
    result[2] = a[0] * b[1] - a[1] * b[0];
}

// Dot product
inline float Dot3(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline double Dot3(const double a[3], const double b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

} // namespace cameraunlock::math
