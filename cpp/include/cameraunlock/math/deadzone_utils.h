#pragma once

#include <cmath>

namespace cameraunlock {
namespace math {

/// Applies deadzone with scaling to prevent jump at threshold.
/// Values within deadzone return 0, values outside are scaled from the edge.
inline double ApplyDeadzone(double value, double deadzone) {
    if (deadzone <= 0.0) {
        return value;
    }

    double abs_value = std::abs(value);
    if (abs_value <= deadzone) {
        return 0.0;
    }

    double sign = value >= 0.0 ? 1.0 : -1.0;
    return sign * (abs_value - deadzone);
}

inline float ApplyDeadzone(float value, float deadzone) {
    if (deadzone <= 0.0f) {
        return value;
    }

    float abs_value = std::abs(value);
    if (abs_value <= deadzone) {
        return 0.0f;
    }

    float sign = value >= 0.0f ? 1.0f : -1.0f;
    return sign * (abs_value - deadzone);
}

}  // namespace math
}  // namespace cameraunlock
