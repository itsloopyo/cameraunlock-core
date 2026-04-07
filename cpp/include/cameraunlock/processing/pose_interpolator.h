#pragma once

#include <cmath>

namespace cameraunlock {

/// Return type for PoseInterpolator — interpolated rotation values in degrees.
struct InterpolatedPose {
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
};

/// Fills in frames between tracking samples using velocity extrapolation with
/// prediction-error correction for silky smooth output at any framerate.
///
/// Sits between the UDP receiver and tracking processor in the pipeline:
///   Raw Pose (60Hz) -> PoseInterpolator -> TrackingProcessor -> Camera
///
/// On each frame, extrapolates from the last sample using EMA-smoothed velocity.
/// When a new sample arrives, the prediction error (difference between what we
/// were outputting and the ground truth) is captured and exponentially blended
/// out over subsequent frames. This eliminates the discontinuity that would occur
/// from snapping to each new sample, spreading the correction across 3-4 frames
/// at high refresh rates (240fps+).
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
    /// @return Interpolated (or extrapolated) pose — smooth at any framerate.
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

                    // Capture prediction error: the gap between what we were
                    // outputting and where the tracker actually is. This gets
                    // exponentially blended out over subsequent frames instead
                    // of causing a hard snap.
                    if (m_hasOutput) {
                        m_correctionYaw   = m_outputYaw   - yaw;
                        m_correctionPitch = m_outputPitch - pitch;
                        m_correctionRoll  = m_outputRoll  - roll;
                    }
                }
            } else {
                // First sample ever — output directly, no correction needed
                m_outputYaw   = yaw;
                m_outputPitch = pitch;
                m_outputRoll  = roll;
                m_hasOutput = true;
            }

            // Store as the new base sample
            m_lastYaw   = yaw;
            m_lastPitch = pitch;
            m_lastRoll  = roll;

            m_timeSinceLastSample = 0.0f;
            m_hasAnySample = true;
        } else {
            // No new sample this frame — accumulate time for extrapolation
            m_timeSinceLastSample += delta_time;
        }

        if (!m_hasAnySample) {
            return {yaw, pitch, roll};
        }

        // Extrapolate from last sample using velocity with decay
        float extrap_time = m_timeSinceLastSample;
        if (extrap_time > max_extrapolation_time) {
            extrap_time = max_extrapolation_time;
        }

        // Decay factor: velocity influence fades as we get further from the last sample.
        // Using 1/(1 + (t/maxT)^2) — nearly full velocity for short gaps (1-2 frames),
        // significant damping only near max extrapolation time.
        float decay = 1.0f;
        if (max_extrapolation_time > 0.0f) {
            float ratio = extrap_time / max_extrapolation_time;
            decay = 1.0f / (1.0f + ratio * ratio);
        }

        float target_yaw   = m_lastYaw;
        float target_pitch = m_lastPitch;
        float target_roll  = m_lastRoll;

        if (m_hasVelocity) {
            target_yaw   += m_velocityYaw   * extrap_time * decay;
            target_pitch += m_velocityPitch * extrap_time * decay;
            target_roll  += m_velocityRoll  * extrap_time * decay;
        }

        // Exponentially decay the correction toward zero.
        // At 240fps (dt=4.17ms): each frame decays 71.3%, fully converged in ~4 frames.
        // At 60fps (dt=16.67ms): 99.3% converged in 1 frame (essentially instant).
        float corrBlend = std::exp(-kCorrectionSpeed * delta_time);
        m_correctionYaw   *= corrBlend;
        m_correctionPitch *= corrBlend;
        m_correctionRoll  *= corrBlend;

        // Output = extrapolated target + diminishing correction.
        // On the frame a new sample arrives, this smoothly blends from where we
        // were (old extrapolation) toward the new truth, instead of snapping.
        m_outputYaw   = target_yaw   + m_correctionYaw;
        m_outputPitch = target_pitch + m_correctionPitch;
        m_outputRoll  = target_roll  + m_correctionRoll;

        return {m_outputYaw, m_outputPitch, m_outputRoll};
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

        m_correctionYaw   = 0.0f;
        m_correctionPitch = 0.0f;
        m_correctionRoll  = 0.0f;

        m_outputYaw   = 0.0f;
        m_outputPitch = 0.0f;
        m_outputRoll  = 0.0f;
        m_hasOutput = false;
    }

private:
    // Velocity EMA blend factor. 0.5 balances responsiveness with jitter filtering.
    static constexpr float kVelocityBlend = 0.5f;

    // Correction decay speed (1/seconds). 300 converges 99.3% within one 60Hz
    // sample period (~16.67ms), spreading the correction across 3-4 frames at
    // 240fps for smooth transitions without perceptible added latency (~2ms).
    static constexpr float kCorrectionSpeed = 300.0f;

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

    // Prediction-error correction: captures the gap between predicted output
    // and ground truth when a new sample arrives, then decays exponentially.
    // Eliminates the 60Hz micro-stutter that snap-on-new-sample would cause.
    float m_correctionYaw   = 0.0f;
    float m_correctionPitch = 0.0f;
    float m_correctionRoll  = 0.0f;

    // Last returned output values (for computing correction on next new sample)
    float m_outputYaw   = 0.0f;
    float m_outputPitch = 0.0f;
    float m_outputRoll  = 0.0f;
    bool m_hasOutput = false;
};

}  // namespace cameraunlock
