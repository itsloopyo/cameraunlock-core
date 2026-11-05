#include "cameraunlock/processing/accela_filter.h"

#include <algorithm>
#include <cmath>

namespace cameraunlock {

namespace {

constexpr double kMinSmoothing = 0.001;
constexpr float kEpsilon = 1e-7f;
constexpr double kPi = 3.14159265358979323846;
// Rust's to_degrees and to_radians multiply by these, rounded once.
constexpr double kDegPerRad = 180.0 / kPi;
constexpr double kRadPerDeg = kPi / 180.0;

// Single-precision trigonometry computed in double and rounded once, as the Rust filter
// (hypari-webcam fmath.rs) does, so the ports agree to the last bit or close to it.
float Sin(float x) { return static_cast<float>(std::sin(static_cast<double>(x))); }
float Cos(float x) { return static_cast<float>(std::cos(static_cast<double>(x))); }
float Acos(float x) { return static_cast<float>(std::acos(static_cast<double>(x))); }
float Atan2(float y, float x) {
    return static_cast<float>(std::atan2(static_cast<double>(y), static_cast<double>(x)));
}

double CopySign(double magnitude, double sign) { return std::copysign(magnitude, sign); }

double Ridge(double t, double lo, double mid, double hi, double pivot) {
    if (t <= pivot) {
        return std::exp(std::log(lo) + (std::log(mid) - std::log(lo)) * (t / pivot));
    }
    return std::exp(std::log(mid) + (std::log(hi) - std::log(mid)) * ((t - pivot) / (1.0 - pivot)));
}

double Curve(const std::vector<std::pair<double, double>>& points, double input) {
    const double x = std::fabs(input);
    if (x < points.front().first) return points.front().second;
    if (x > points.back().first) return points.back().second;
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        const auto& p0 = points[i];
        const auto& p1 = points[i + 1];
        if (x >= p0.first && x <= p1.first) {
            return p0.second + (x - p0.first) / (p1.first - p0.first) * (p1.second - p0.second);
        }
    }
    return points.back().second;
}

const std::vector<std::pair<double, double>>& RotationGain() {
    static const std::vector<std::pair<double, double>> curve = {
        {0.0, 0.0}, {0.2, 0.05}, {0.4, 0.15}, {0.7, 0.4}, {1.0, 0.9}, {1.5, 2.0},
        {2.2, 4.5}, {3.0, 8.0}, {4.0, 12.0}, {5.0, 16.0}, {6.5, 21.0}, {8.0, 25.0},
    };
    return curve;
}

const std::vector<std::pair<double, double>>& PositionGain() {
    static const std::vector<std::pair<double, double>> curve = {
        {0.0, 0.0}, {0.2, 0.05}, {0.5, 0.2}, {0.8, 0.5}, {1.2, 1.2}, {1.8, 3.0},
        {2.6, 6.0}, {3.5, 10.0}, {4.5, 14.0}, {6.0, 19.0}, {7.5, 24.0}, {9.5, 30.0},
    };
    return curve;
}

}  // namespace

AccelaFilter::Params AccelaFilter::ParamsFor(double amount) {
    const double t = std::clamp(amount, 0.0, 1.0);
    // A higher divisor is what buys heaviness; the deadzone is a jitter gate and barely
    // moves, since a large one freezes the output until the error clears it.
    return Params{
        Ridge(t, 0.01, 0.03, 0.06, 0.5),
        Ridge(t, 0.01, 0.02, 0.05, 0.5),
        Ridge(t, 0.3, 1.4, 2.4, 0.5),
        Ridge(t, 0.3, 1.6, 2.6, 0.5),
    };
}

AccelaFilter::AccelaFilter(double amount) : m_amount(std::clamp(amount, 0.0, 1.0)), m_params(ParamsFor(amount)) {
    Reset();
}

void AccelaFilter::SetAmount(double amount) {
    m_amount = std::clamp(amount, 0.0, 1.0);
    m_params = ParamsFor(m_amount);
}

void AccelaFilter::Reset() {
    m_position = {};
    // The gain floor makes a small depth step settle within a second at 15 to 60 Hz
    // (headcam-android's DepthPositionGainTest).
    m_position[2].minimumGain = 4.0;
    m_rotation = Rotation{};
}

double AccelaFilter::Axis::Filter(double value, double time, double deadzone, double smoothing,
                                  const std::vector<std::pair<double, double>>& curve) {
    if (lastTime == 0.0) {
        lastOutput = value;
        lastInput = value;
        lastTime = time;
        return value;
    }
    const double rawDt = time - lastTime;
    if (rawDt <= 0.0) return lastOutput;
    const double dt = std::min(rawDt, kMaxDt);
    const unsigned steps = static_cast<unsigned>(std::ceil(dt / kStep));
    const double stepDt = dt / steps;
    const double inputDelta = value - lastInput;
    for (unsigned step = 1; step <= steps; ++step) {
        const double sampled = steps == 1 ? value : lastInput + inputDelta * step / steps;
        const double full = sampled - lastOutput;
        if (std::fabs(full) < deadzone) continue;
        const double delta = full - CopySign(deadzone, full);
        const double normalized = delta / std::max(smoothing, kMinSmoothing);
        const double gain = std::max(minimumGain, Curve(curve, std::fabs(normalized)));
        const double filtered = CopySign(std::fabs(normalized), delta) * gain;
        const double rawStep = filtered * smoothing * stepDt;
        // Never step past the input.
        lastOutput += CopySign(std::min(std::fabs(rawStep), std::fabs(full)), full);
    }
    lastInput = value;
    lastTime = time;
    return lastOutput;
}

AccelaFilter::Quat AccelaFilter::Rotation::Filter(Quat q, double time, double deadzone, double smoothing,
                                                  const std::vector<std::pair<double, double>>& curve) {
    auto dot = [](const Quat& a, const Quat& b) { return a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z; };
    auto neg = [](const Quat& a) { return Quat{-a.w, -a.x, -a.y, -a.z}; };
    auto mul = [](const Quat& a, const Quat& b) {
        return Quat{
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        };
    };
    auto normalize = [&](const Quat& a) {
        const float len = std::sqrt(dot(a, a));
        if (len < kEpsilon) return Quat{};
        return Quat{a.w / len, a.x / len, a.y / len, a.z / len};
    };
    auto slerp = [&](const Quat& q0, const Quat& q1, float t) {
        float d = dot(q0, q1);
        Quat target = q1;
        if (d < 0.0f) {
            d = -d;
            target = neg(q1);
        }
        if (d > 0.9995f) {
            return normalize(Quat{q0.w + t * (target.w - q0.w), q0.x + t * (target.x - q0.x),
                                  q0.y + t * (target.y - q0.y), q0.z + t * (target.z - q0.z)});
        }
        const float theta0 = Acos(std::clamp(d, -1.0f, 1.0f));
        const float theta = theta0 * t;
        const float sinTheta = Sin(theta);
        const float sinTheta0 = Sin(theta0);
        const float s0 = Cos(theta) - d * sinTheta / sinTheta0;
        const float s1 = sinTheta / sinTheta0;
        return normalize(Quat{s0 * q0.w + s1 * target.w, s0 * q0.x + s1 * target.x, s0 * q0.y + s1 * target.y,
                              s0 * q0.z + s1 * target.z});
    };

    if (lastTime == 0.0) {
        lastOutput = q;
        lastInput = q;
        lastTime = time;
        return q;
    }
    const double rawDt = time - lastTime;
    if (rawDt <= 0.0) return lastOutput;
    const double dt = std::min(rawDt, kMaxDt);
    const unsigned steps = static_cast<unsigned>(std::ceil(dt / kStep));
    const double stepDt = dt / steps;
    for (unsigned step = 1; step <= steps; ++step) {
        const Quat sampled = steps == 1 ? q : slerp(lastInput, q, static_cast<float>(step) / static_cast<float>(steps));
        const Quat input = dot(sampled, lastOutput) < 0.0f ? neg(sampled) : sampled;
        const Quat inverse{lastOutput.w, -lastOutput.x, -lastOutput.y, -lastOutput.z};
        const Quat deltaQ = mul(input, inverse);
        // The vector part keeps tiny rotations after the scalar has rounded to one.
        const float sinHalf = std::sqrt(deltaQ.x * deltaQ.x + deltaQ.y * deltaQ.y + deltaQ.z * deltaQ.z);
        const double deltaAngle = static_cast<double>(2.0f * Atan2(sinHalf, std::fabs(deltaQ.w))) * kDegPerRad;
        float axis[3] = {0.0f, 1.0f, 0.0f};
        {
            const float sign = deltaQ.w < 0.0f ? -1.0f : 1.0f;
            const float qx = deltaQ.x * sign, qy = deltaQ.y * sign, qz = deltaQ.z * sign;
            const float s = std::sqrt(qx * qx + qy * qy + qz * qz);
            if (s >= kEpsilon) {
                axis[0] = qx / s;
                axis[1] = qy / s;
                axis[2] = qz / s;
            }
        }
        if (std::fabs(deltaAngle) < deadzone) continue;
        const double dz = deltaAngle - CopySign(deadzone, deltaAngle);
        const double normalized = dz / std::max(smoothing, kMinSmoothing);
        const double gain = Curve(curve, std::fabs(normalized));
        const double filtered = CopySign(std::fabs(normalized), dz) * gain;
        const double rawAngle = filtered * smoothing * stepDt;
        const float angle = static_cast<float>(std::min(std::fabs(rawAngle), std::fabs(deltaAngle)) * kRadPerDeg);
        // deltaQ is in the world frame, so the partial step pre-multiplies.
        Quat stepQ{};
        {
            const float half = angle / 2.0f;
            const float s = Sin(half);
            const float len = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
            if (len >= kEpsilon) stepQ = Quat{Cos(half), axis[0] / len * s, axis[1] / len * s, axis[2] / len * s};
        }
        lastOutput = normalize(mul(stepQ, lastOutput));
    }
    lastInput = q;
    lastTime = time;
    return lastOutput;
}

// Intrinsic Y-X-Z (yaw, pitch, roll) in degrees, the OpenTrack convention:
// M = Ry(yaw) * Rx(pitch) * Rz(-roll).
AccelaFilter::Quat AccelaFilter::FromEulerYxz(double yaw, double pitch, double roll) {
    const float hy = static_cast<float>(yaw * kRadPerDeg / 2.0);
    const float hx = static_cast<float>(pitch * kRadPerDeg / 2.0);
    const float hz = static_cast<float>(-(roll * kRadPerDeg) / 2.0);
    const float cy = Cos(hy), sy = Sin(hy);
    const float cx = Cos(hx), sx = Sin(hx);
    const float cz = Cos(hz), sz = Sin(hz);
    Quat q{
        cy * cx * cz + sy * sx * sz,
        cy * sx * cz + sy * cx * sz,
        sy * cx * cz - cy * sx * sz,
        cy * cx * sz - sy * sx * cz,
    };
    const float len = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    if (len < kEpsilon) return Quat{};
    return Quat{q.w / len, q.x / len, q.y / len, q.z / len};
}

void AccelaFilter::ToEulerYxz(Quat q, double& yaw, double& pitch, double& roll) {
    const float w = q.w, x = q.x, y = q.y, z = q.z;
    const float xx = x * x, yy = y * y, zz = z * z;
    const float xy = x * y, xz = x * z, yz = y * z;
    const float wx = w * x, wy = w * y, wz = w * z;
    // Column-major m[col * 3 + row], as the Rust filter has it.
    const float m[9] = {
        1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz),        2.0f * (xz - wy),
        2.0f * (xy - wz),        1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx),
        2.0f * (xz + wy),        2.0f * (yz - wx),        1.0f - 2.0f * (xx + yy),
    };
    const double sinPitch = -static_cast<double>(m[7]);
    double rx, ry, rz;
    if (std::fabs(sinPitch) >= 0.99999) {
        // Pitch at +-90 degrees: only yaw +- roll is observable, so roll is set to zero.
        rx = (sinPitch < 0.0 ? -1.0 : 1.0) * kPi / 2.0;
        rz = 0.0;
        ry = std::atan2(-static_cast<double>(m[2]), static_cast<double>(m[0]));
    } else {
        rx = std::asin(sinPitch);
        ry = std::atan2(static_cast<double>(m[6]), static_cast<double>(m[8]));
        rz = std::atan2(static_cast<double>(m[1]), static_cast<double>(m[4]));
    }
    yaw = ry * kDegPerRad;
    pitch = rx * kDegPerRad;
    roll = -(rz * kDegPerRad);
}

AccelaPose AccelaFilter::Filter(const AccelaPose& input, double timeSeconds) {
    const Quat q = m_rotation.Filter(FromEulerYxz(input.yaw, input.pitch, input.roll), timeSeconds,
                                     m_params.rotationDeadzone, m_params.rotationSmoothing, RotationGain());
    AccelaPose out;
    ToEulerYxz(q, out.yaw, out.pitch, out.roll);
    out.x = m_position[0].Filter(input.x, timeSeconds, m_params.positionDeadzone, m_params.positionSmoothing, PositionGain());
    out.y = m_position[1].Filter(input.y, timeSeconds, m_params.positionDeadzone, m_params.positionSmoothing, PositionGain());
    out.z = m_position[2].Filter(input.z, timeSeconds, m_params.positionDeadzone, m_params.positionSmoothing, PositionGain());
    return out;
}

}  // namespace cameraunlock
