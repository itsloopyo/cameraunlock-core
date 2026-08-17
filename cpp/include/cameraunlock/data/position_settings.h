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
    /// Maximum Y displacement upward.
    float limit_y = 0.20f;
    /// Maximum Y displacement DOWNWARD. Restricts how far below eye height the camera
    /// can travel. Was missing entirely here while C# has always had it, so an
    /// asymmetric vertical limit simply could not be expressed on this side and a
    /// ported config silently widened the downward budget into player-body clipping.
    float limit_y_down = 0.20f;
    float limit_z = 0.40f;
    float limit_z_back = 0.10f;
    float local_smoothing = static_cast<float>(math::kDefaultLocalSmoothing);
    float remote_smoothing = static_cast<float>(math::kDefaultRemoteSmoothing);
    bool invert_x = false;
    bool invert_y = false;
    bool invert_z = false;

    PositionSettings() = default;

    /// The full asymmetric form, mirroring the single C# constructor. There is
    /// deliberately no second overload: C# removed its symmetric one because the two
    /// arities ended up adjacent and a stale positional call silently rebound one slot
    /// to the left, turning a lean limit into a smoothing value with no compiler signal.
    /// Use Symmetric() for the symmetric case.
    PositionSettings(float sens_x, float sens_y, float sens_z,
                     float lim_x, float lim_y, float lim_y_down, float lim_z, float lim_z_back,
                     float local_smooth, float remote_smooth,
                     bool inv_x = false, bool inv_y = false, bool inv_z = false)
        : sensitivity_x(sens_x), sensitivity_y(sens_y), sensitivity_z(sens_z)
        , limit_x(lim_x), limit_y(lim_y), limit_y_down(lim_y_down)
        , limit_z(lim_z), limit_z_back(lim_z_back)
        , local_smoothing(local_smooth), remote_smoothing(remote_smooth)
        , invert_x(inv_x), invert_y(inv_y), invert_z(inv_z) {}

    /// Symmetric vertical limits: lim_y is mirrored into limit_y_down.
    static PositionSettings Symmetric(float sens_x, float sens_y, float sens_z,
                                      float lim_x, float lim_y, float lim_z, float lim_z_back,
                                      float local_smooth, float remote_smooth,
                                      bool inv_x = false, bool inv_y = false, bool inv_z = false) {
        return PositionSettings(sens_x, sens_y, sens_z, lim_x, lim_y, lim_y, lim_z, lim_z_back,
                                local_smooth, remote_smooth, inv_x, inv_y, inv_z);
    }

    static PositionSettings Default() {
        return Symmetric(1.0f, 1.0f, 1.0f, 0.30f, 0.20f, 0.40f, 0.10f,
                         static_cast<float>(math::kDefaultLocalSmoothing),
                         static_cast<float>(math::kDefaultRemoteSmoothing));
    }
};

}  // namespace cameraunlock
