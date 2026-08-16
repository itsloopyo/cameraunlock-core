#pragma once

#include "cameraunlock/math/smoothing_utils.h"

namespace cameraunlock {

/// Settings for positional tracking: per-axis sensitivity, limits, smoothing, and
/// inversion. Port of CameraUnlock.Core.Data.PositionSettings (C#). The connection
/// flag that selects between the two smoothing values is NOT here: it is runtime
/// state, not a user setting, so it lives on PositionProcessor as
/// is_remote_connection and a settings rebuild never has to re-supply it.
struct PositionSettings {
    float sensitivity_x = 1.0f;
    float sensitivity_y = 1.0f;
    float sensitivity_z = 1.0f;
    float limit_x = 0.30f;
    float limit_y = 0.20f;
    float limit_z = 0.40f;
    float limit_z_back = 0.10f;
    float local_smoothing = static_cast<float>(math::kDefaultLocalSmoothing);
    float remote_smoothing = static_cast<float>(math::kDefaultRemoteSmoothing);
    bool invert_x = false;
    bool invert_y = false;
    bool invert_z = false;

    PositionSettings() = default;

    PositionSettings(float sens_x, float sens_y, float sens_z,
                     float lim_x, float lim_y, float lim_z, float lim_z_back,
                     float local_smooth, float remote_smooth,
                     bool inv_x = false, bool inv_y = false, bool inv_z = false)
        : sensitivity_x(sens_x), sensitivity_y(sens_y), sensitivity_z(sens_z)
        , limit_x(lim_x), limit_y(lim_y), limit_z(lim_z), limit_z_back(lim_z_back)
        , local_smoothing(local_smooth), remote_smoothing(remote_smooth)
        , invert_x(inv_x), invert_y(inv_y), invert_z(inv_z) {}

    static PositionSettings Default() {
        return PositionSettings(1.0f, 1.0f, 1.0f, 0.30f, 0.20f, 0.40f, 0.10f,
                                static_cast<float>(math::kDefaultLocalSmoothing),
                                static_cast<float>(math::kDefaultRemoteSmoothing));
    }
};

}  // namespace cameraunlock
