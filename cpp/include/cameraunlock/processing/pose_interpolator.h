#pragma once

namespace cameraunlock {

/// Return type for PoseInterpolator — interpolated rotation values in degrees.
struct InterpolatedPose {
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
};

/// Fills in frames between tracking samples using linear interpolation.
/// Buffers one sample and lerps between the previous and current known positions,
/// trading one sample period of latency (~8ms at 60Hz) for guaranteed smooth output
/// at any display refresh rate.
///
/// Sits between the UDP receiver and tracking processor in the pipeline:
///   Raw Pose (60Hz) -> PoseInterpolator -> TrackingProcessor -> Camera
///
/// Port of CameraUnlock.Core.Processing.PoseInterpolator (C#).
class PoseInterpolator {
public:
    /// Kept for API compatibility.
    float max_extrapolation_time = 0.1f;

    PoseInterpolator() = default;

    /// Update with the latest raw pose and frame timing.
    /// @param yaw Raw yaw in degrees.
    /// @param pitch Raw pitch in degrees.
    /// @param roll Raw roll in degrees.
    /// @param is_new_sample True if the receiver got fresh data this frame.
    /// @param delta_time Frame delta time in seconds.
    /// @return Smoothly interpolated pose.
    inline InterpolatedPose Update(float yaw, float pitch, float roll,
                                   bool is_new_sample, float delta_time) {
        m_timeSinceLastNewSample += delta_time;

        if (is_new_sample) {
            if (!m_hasFirstSample) {
                // Very first sample — park at this position
                m_fromYaw = yaw;    m_fromPitch = pitch;    m_fromRoll = roll;
                m_toYaw = yaw;      m_toPitch = pitch;      m_toRoll = roll;
                m_progress = 1.0f;
                m_timeSinceLastNewSample = 0.0f;
                m_hasFirstSample = true;
                return {yaw, pitch, roll};
            }

            // Update sample interval estimate (EMA)
            if (m_timeSinceLastNewSample > kMinSampleInterval) {
                if (!m_hasSecondSample) {
                    m_sampleInterval = m_timeSinceLastNewSample;
                    m_hasSecondSample = true;
                } else {
                    m_sampleInterval += (m_timeSinceLastNewSample - m_sampleInterval) * kIntervalBlend;
                }
                if (m_sampleInterval < kMinSampleInterval) m_sampleInterval = kMinSampleInterval;
                if (m_sampleInterval > kMaxSampleInterval) m_sampleInterval = kMaxSampleInterval;
            }

            // Capture current interpolated position as new start point
            float t = m_progress > 1.0f ? 1.0f : m_progress;
            m_fromYaw   = m_fromYaw   + (m_toYaw   - m_fromYaw)   * t;
            m_fromPitch = m_fromPitch + (m_toPitch - m_fromPitch) * t;
            m_fromRoll  = m_fromRoll  + (m_toRoll  - m_fromRoll)  * t;

            // New sample becomes the target
            m_toYaw = yaw;    m_toPitch = pitch;    m_toRoll = roll;
            m_progress = 0.0f;
            m_timeSinceLastNewSample = 0.0f;
        }

        if (!m_hasFirstSample) {
            return {yaw, pitch, roll};
        }

        // Advance interpolation
        m_progress += delta_time / m_sampleInterval;

        // Clamp — hold at target when waiting for next sample
        float pt = m_progress > 1.0f ? 1.0f : (m_progress < 0.0f ? 0.0f : m_progress);

        float outYaw   = m_fromYaw   + (m_toYaw   - m_fromYaw)   * pt;
        float outPitch = m_fromPitch + (m_toPitch - m_fromPitch) * pt;
        float outRoll  = m_fromRoll  + (m_toRoll  - m_fromRoll)  * pt;

        return {outYaw, outPitch, outRoll};
    }

    /// Resets all interpolation state. Call on recenter, scene transitions, or tracking re-enable.
    inline void Reset() {
        m_fromYaw = 0.0f;    m_fromPitch = 0.0f;    m_fromRoll = 0.0f;
        m_toYaw = 0.0f;      m_toPitch = 0.0f;      m_toRoll = 0.0f;
        m_progress = 0.0f;
        m_sampleInterval = kDefaultSampleInterval;
        m_timeSinceLastNewSample = 0.0f;
        m_hasFirstSample = false;
        m_hasSecondSample = false;
    }

private:
    // EMA blend factor for sample interval estimation
    static constexpr float kIntervalBlend = 0.3f;
    // Default until we observe real samples
    static constexpr float kDefaultSampleInterval = 1.0f / 60.0f;
    // Bounds for sample interval estimate
    static constexpr float kMinSampleInterval = 0.001f;
    static constexpr float kMaxSampleInterval = 0.2f;

    // Interpolation segment: lerp from → to
    float m_fromYaw = 0.0f, m_fromPitch = 0.0f, m_fromRoll = 0.0f;
    float m_toYaw = 0.0f, m_toPitch = 0.0f, m_toRoll = 0.0f;

    // Progress within current segment (0 = at from, 1 = at to)
    float m_progress = 0.0f;

    // EMA-smoothed estimate of time between tracker samples
    float m_sampleInterval = kDefaultSampleInterval;

    // Accumulated wall time since last new sample arrived
    float m_timeSinceLastNewSample = 0.0f;

    bool m_hasFirstSample = false;
    bool m_hasSecondSample = false;
};

}  // namespace cameraunlock
