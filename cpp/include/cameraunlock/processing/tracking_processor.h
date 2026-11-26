#pragma once

#include "cameraunlock/data/tracking_pose.h"
#include "cameraunlock/math/quat4.h"
#include "cameraunlock/math/smoothing_utils.h"
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

    /// Smoothing applied when the tracker runs on this machine (loopback).
    void SetLocalSmoothing(float smoothing) { m_localSmoothing = smoothing; }

    /// Smoothing applied when the tracker is a remote device on the network.
    void SetRemoteSmoothing(float smoothing) { m_remoteSmoothing = smoothing; }

    /// Fed from the receiver's IsRemoteConnection() every update so a switch
    /// between a local tracker and a remote one picks up the other parameter
    /// without a restart.
    void SetIsRemoteConnection(bool is_remote) { m_isRemoteConnection = is_remote; }

    const SensitivitySettings& GetSensitivity() const { return m_sensitivity; }
    const DeadzoneSettings& GetDeadzone() const { return m_deadzone; }
    float GetLocalSmoothing() const { return m_localSmoothing; }
    float GetRemoteSmoothing() const { return m_remoteSmoothing; }
    bool IsRemoteConnection() const { return m_isRemoteConnection; }

    /// Gets the center offset manager.
    CenterOffsetManager& GetCenterManager() { return m_centerManager; }

    /// The centered, smoothed rotation BEFORE per-axis sensitivity and inversion.
    /// This is the physical head rotation, and the value PositionProcessor needs for
    /// pivot compensation.
    void GetSmoothedRotation(float& yaw, float& pitch, float& roll) const {
        yaw = m_smoothedYaw;
        pitch = m_smoothedPitch;
        roll = m_smoothedRoll;
    }

    /// Resets only the smoothing state, preserving the centre offset.
    void ResetSmoothing();

private:
    CenterOffsetManager m_centerManager;

    // Smoothed rotation held as per-axis Euler, matching the C# port. Quaternion SLERP
    // was used here and introduced a roll term on compound movement that the C# side
    // does not produce; see Process() for the full rationale.
    float m_smoothedYaw = 0.0f;
    float m_smoothedPitch = 0.0f;
    float m_smoothedRoll = 0.0f;
    bool m_hasSmoothedValue = false;

    // Configuration
    SensitivitySettings m_sensitivity = SensitivitySettings::Default();
    DeadzoneSettings m_deadzone = DeadzoneSettings::None();
    float m_localSmoothing = static_cast<float>(math::kDefaultLocalSmoothing);
    float m_remoteSmoothing = static_cast<float>(math::kDefaultRemoteSmoothing);
    bool m_isRemoteConnection = false;
};

}  // namespace cameraunlock
