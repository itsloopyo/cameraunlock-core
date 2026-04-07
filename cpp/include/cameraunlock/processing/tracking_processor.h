#pragma once

#include "cameraunlock/data/tracking_pose.h"
#include "cameraunlock/math/quat4.h"
#include "cameraunlock/processing/center_offset_manager.h"

namespace cameraunlock {

/// Complete tracking data processing pipeline.
/// Pipeline: raw -> offset -> deadzone -> smooth -> sensitivity
class TrackingProcessor {
public:
    TrackingProcessor() = default;

    /// Processes raw rotation values through the full pipeline.
    /// @param yaw Raw yaw in degrees.
    /// @param pitch Raw pitch in degrees.
    /// @param roll Raw roll in degrees.
    /// @param delta_time Time since last frame in seconds.
    /// @return Processed pose.
    TrackingPose Process(float yaw, float pitch, float roll, float delta_time);

    /// Sets the current smoothed pose as the center.
    void Recenter();

    /// Sets specific values as the center.
    void RecenterTo(float yaw, float pitch, float roll);

    /// Resets the processor state.
    void Reset();

    // Configuration
    void SetSensitivity(const SensitivitySettings& sensitivity) { m_sensitivity = sensitivity; }
    void SetDeadzone(const DeadzoneSettings& deadzone) { m_deadzone = deadzone; }
    void SetSmoothing(float smoothing) { m_smoothingFactor = smoothing; }

    const SensitivitySettings& GetSensitivity() const { return m_sensitivity; }
    const DeadzoneSettings& GetDeadzone() const { return m_deadzone; }
    float GetSmoothing() const { return m_smoothingFactor; }

    /// Gets the center offset manager.
    CenterOffsetManager& GetCenterManager() { return m_centerManager; }

    /// Gets the current smoothed values.
    void GetSmoothedRotation(float& yaw, float& pitch, float& roll) const {
        m_smoothedQuat.ToEulerYXZ(yaw, pitch, roll);
    }

private:
    CenterOffsetManager m_centerManager;

    // Smoothed rotation as quaternion — SLERP avoids gimbal artifacts
    // that per-axis Euler smoothing can introduce at compound angles.
    math::Quat4 m_smoothedQuat;
    bool m_hasSmoothedValue = false;

    // Configuration
    SensitivitySettings m_sensitivity = SensitivitySettings::Default();
    DeadzoneSettings m_deadzone = DeadzoneSettings::None();
    float m_smoothingFactor = 0.0f;
};

}  // namespace cameraunlock
