#pragma once

namespace cameraunlock {

/// Return type for PoseInterpolator — interpolated rotation values in degrees.
struct InterpolatedPose {
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
};

/// Fills in frames between low-rate tracking samples using velocity extrapolation.
/// Sits between the UDP receiver and tracking processor in the pipeline:
///   Raw Pose (variable Hz) -> PoseInterpolator -> TrackingProcessor -> Camera
///
/// When tracking rate >= game FPS, every frame has a new sample and the interpolator
/// passes data through unchanged. When tracking rate < game FPS, stale frames are
/// extrapolated using EMA-smoothed velocity with decay.
///
/// Ported from CameraUnlock.Core.Processing.PoseInterpolator (obra-dinn).
class PoseInterpolator {
public:
    /// Maximum time (seconds) to extrapolate beyond the last sample.
    /// Prevents runaway drift when tracking is lost or stalled.
    float max_extrapolation_time = 0.1f;

    PoseInterpolator() = default;

    /// Update with the latest raw pose and frame timing.
    /// @param yaw Raw yaw in degrees.
    /// @param pitch Raw pitch in degrees.
    /// @param roll Raw roll in degrees.
    /// @param is_new_sample True if the receiver got fresh data this frame.
    /// @param delta_time Frame delta time in seconds.
    /// @return Interpolated (or extrapolated) pose.
    inline InterpolatedPose Update(float yaw, float pitch, float roll,
                                   bool is_new_sample, float delta_time) {
        if (is_new_sample) {
            if (m_hasAnySample) {
                // Compute velocity from the full inter-sample interval.
                // m_timeSinceLastSample only accumulated stale-frame deltas;
                // add this frame's delta_time to get the true elapsed time
                // between consecutive new samples.
                float sample_dt = m_timeSinceLastSample + delta_time;
                if (sample_dt > 0.0f) {
                    float inst_vel_yaw   = (yaw   - m_lastYaw)   / sample_dt;
                    float inst_vel_pitch = (pitch - m_lastPitch) / sample_dt;
                    float inst_vel_roll  = (roll  - m_lastRoll)  / sample_dt;

                    if (m_hasVelocity) {
                        // EMA smooth the velocity estimate
                        m_velocityYaw   += (inst_vel_yaw   - m_velocityYaw)   * kVelocityBlend;
                        m_velocityPitch += (inst_vel_pitch - m_velocityPitch) * kVelocityBlend;
                        m_velocityRoll  += (inst_vel_roll  - m_velocityRoll)  * kVelocityBlend;
                    } else {
                        m_velocityYaw   = inst_vel_yaw;
                        m_velocityPitch = inst_vel_pitch;
                        m_velocityRoll  = inst_vel_roll;
                        m_hasVelocity = true;
                    }
                }
            }

            // Store as the new base sample
            m_lastYaw   = yaw;
            m_lastPitch = pitch;
            m_lastRoll  = roll;

            m_timeSinceLastSample = 0.0f;
            m_hasAnySample = true;

            // New sample — return it directly
            return {yaw, pitch, roll};
        }

        // No new sample this frame — extrapolate if we have velocity
        m_timeSinceLastSample += delta_time;

        if (!m_hasVelocity) {
            return {yaw, pitch, roll};
        }

        // Cap extrapolation time to prevent drift
        float extrap_time = m_timeSinceLastSample;
        if (extrap_time > max_extrapolation_time) {
            extrap_time = max_extrapolation_time;
        }

        // Decay factor: velocity influence fades as we get further from the last sample.
        // Using 1/(1 + (t/maxT)^2) — nearly full velocity for short gaps (1-2 frames),
        // significant damping only near max extrapolation time. This keeps frame-to-frame
        // deltas even during normal operation while still preventing overshoot on stops.
        //   r=0.17 (1 frame @ 30Hz/60fps): decay=0.97  (almost no loss)
        //   r=0.50:                         decay=0.80
        //   r=1.00 (at max time):           decay=0.50
        float decay = 1.0f;
        if (max_extrapolation_time > 0.0f) {
            float ratio = extrap_time / max_extrapolation_time;
            decay = 1.0f / (1.0f + ratio * ratio);
        }

        float pred_yaw   = m_lastYaw   + m_velocityYaw   * extrap_time * decay;
        float pred_pitch = m_lastPitch + m_velocityPitch * extrap_time * decay;
        float pred_roll  = m_lastRoll  + m_velocityRoll  * extrap_time * decay;

        return {pred_yaw, pred_pitch, pred_roll};
    }

    /// Resets all interpolation state. Call on recenter, scene transitions, or tracking re-enable.
    inline void Reset() {
        m_lastYaw   = 0.0f;
        m_lastPitch = 0.0f;
        m_lastRoll  = 0.0f;

        m_velocityYaw   = 0.0f;
        m_velocityPitch = 0.0f;
        m_velocityRoll  = 0.0f;
        m_hasVelocity = false;

        m_timeSinceLastSample = 0.0f;
        m_hasAnySample = false;
    }

private:
    // Velocity EMA blend factor. 0.5 balances responsiveness with jitter filtering.
    static constexpr float kVelocityBlend = 0.5f;

    // Last accepted sample (used as extrapolation base)
    float m_lastYaw   = 0.0f;
    float m_lastPitch = 0.0f;
    float m_lastRoll  = 0.0f;

    // Smoothed velocity (degrees per second)
    float m_velocityYaw   = 0.0f;
    float m_velocityPitch = 0.0f;
    float m_velocityRoll  = 0.0f;
    bool m_hasVelocity = false;

    // Accumulated time since last new sample
    float m_timeSinceLastSample = 0.0f;

    bool m_hasAnySample = false;
};

}  // namespace cameraunlock
