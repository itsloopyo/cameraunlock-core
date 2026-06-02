#pragma once

#include <cmath>

namespace cameraunlock {
namespace math {

/// Minimum smoothing floor applied to all connections.
/// Must match BaselineSmoothing in C# SmoothingUtils.cs.
/// 0.15 gives ~40% per frame at 60fps, settling in ~100-150ms.
constexpr double kBaselineSmoothing = 0.15;

/// Linear interpolation.
inline double Lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

inline float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

/// Calculates the smoothing interpolation factor for the current frame.
/// Uses frame-rate independent exponential smoothing.
/// @param smoothing Smoothing factor 0-1. 0=instant, 1=very slow.
/// @param delta_time Time since last frame in seconds.
/// @return Interpolation factor to use with Lerp/Slerp.
inline double CalculateSmoothingFactor(double smoothing, double delta_time) {
    if (smoothing < 0.001) {
        return 1.0;  // No smoothing, snap to target
    }

    // Map smoothing 0->50 speed, 1->0.1 speed
    double smoothing_speed = Lerp(50.0, 0.1, smoothing);
    return 1.0 - std::exp(-smoothing_speed * delta_time);
}

inline float CalculateSmoothingFactor(float smoothing, float delta_time) {
    if (smoothing < 0.001f) {
        return 1.0f;
    }
    float smoothing_speed = Lerp(50.0f, 0.1f, smoothing);
    return 1.0f - std::exp(-smoothing_speed * delta_time);
}

/// Applies smoothing to a single value.
inline double Smooth(double current, double target, double smoothing, double delta_time) {
    double t = CalculateSmoothingFactor(smoothing, delta_time);
    return current + (target - current) * t;
}

inline float Smooth(float current, float target, float smoothing, float delta_time) {
    float t = CalculateSmoothingFactor(smoothing, delta_time);
    return current + (target - current) * t;
}

/// Gets the effective smoothing factor, ensuring the baseline floor is always applied.
inline double GetEffectiveSmoothing(double base_smoothing) {
    if (base_smoothing < kBaselineSmoothing) {
        return kBaselineSmoothing;
    }
    return base_smoothing;
}

/// Exponentially smoothed scalar that snaps to the first sample instead of
/// blending up from zero. Replaces the per-call-site "static value + init
/// flag + Lerp" boilerplate around screen-space projection values.
class SmoothedFloat {
public:
    /// Feed the next raw sample and return the smoothed value.
    float Update(float target, float smoothing, float delta_time) {
        if (!m_initialized) {
            m_value = target;
            m_initialized = true;
            return m_value;
        }
        m_value = Smooth(m_value, target, smoothing, delta_time);
        return m_value;
    }

    float Get() const { return m_value; }
    bool IsInitialized() const { return m_initialized; }

    /// Forget the current value; the next Update snaps to its sample.
    void Reset() {
        m_value = 0.0f;
        m_initialized = false;
    }

private:
    float m_value = 0.0f;
    bool m_initialized = false;
};

}  // namespace math
}  // namespace cameraunlock
