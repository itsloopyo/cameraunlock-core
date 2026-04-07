#pragma once

#include <cmath>
#include "cameraunlock/data/position_data.h"

namespace cameraunlock {

/// Fills in frames between low-rate position samples using velocity extrapolation
/// with prediction-error correction for smooth output at any framerate.
///
/// Same algorithm as PoseInterpolator but for 3D position (x, y, z).
/// When a new sample arrives, the prediction error is captured and exponentially
/// blended out over subsequent frames — no hard snap at sample boundaries.
class PositionInterpolator {
public:
    PositionInterpolator() = default;

    /// Maximum time (seconds) to extrapolate beyond the last sample.
    float GetMaxExtrapolationTime() const { return m_maxExtrapolationTime; }
    void SetMaxExtrapolationTime(float value) { m_maxExtrapolationTime = value; }

    /// Update with the latest raw position and frame delta time.
    /// Returns an interpolated (extrapolated) position — smooth at any framerate.
    PositionData Update(const PositionData& raw, float delta_time) {
        if (!raw.IsValid()) {
            return raw;
        }

        bool is_new_sample = raw.timestamp_us != m_lastTimestampUs;

        if (is_new_sample) {
            if (m_hasAnySample) {
                // Include current frame's delta — m_timeSinceLastSample only has
                // stale-frame deltas accumulated so far, missing this frame.
                float sample_dt = m_timeSinceLastSample + delta_time;
                if (sample_dt > 0.0f) {
                    float inst_vel_x = (raw.x - m_lastX) / sample_dt;
                    float inst_vel_y = (raw.y - m_lastY) / sample_dt;
                    float inst_vel_z = (raw.z - m_lastZ) / sample_dt;

                    if (m_hasVelocity) {
                        m_velocityX += (inst_vel_x - m_velocityX) * kVelocityBlend;
                        m_velocityY += (inst_vel_y - m_velocityY) * kVelocityBlend;
                        m_velocityZ += (inst_vel_z - m_velocityZ) * kVelocityBlend;
                    } else {
                        m_velocityX = inst_vel_x;
                        m_velocityY = inst_vel_y;
                        m_velocityZ = inst_vel_z;
                        m_hasVelocity = true;
                    }

                    // Capture prediction error for smooth blending
                    if (m_hasOutput) {
                        m_correctionX = m_outputX - raw.x;
                        m_correctionY = m_outputY - raw.y;
                        m_correctionZ = m_outputZ - raw.z;
                    }
                }
            } else {
                // First sample ever — output directly
                m_outputX = raw.x;
                m_outputY = raw.y;
                m_outputZ = raw.z;
                m_hasOutput = true;
            }

            m_lastTimestampUs = raw.timestamp_us;
            m_lastX = raw.x;
            m_lastY = raw.y;
            m_lastZ = raw.z;

            m_timeSinceLastSample = 0.0f;
            m_hasAnySample = true;
        } else {
            m_timeSinceLastSample += delta_time;
        }

        if (!m_hasAnySample) {
            return raw;
        }

        float extrap_time = m_timeSinceLastSample;
        if (extrap_time > m_maxExtrapolationTime) {
            extrap_time = m_maxExtrapolationTime;
        }

        // Decay factor: velocity influence fades as we get further from the last sample
        // Uses 1/(1+r^2) — gentle near 0, only dampens near max extrapolation time.
        float decay = 1.0f;
        if (m_maxExtrapolationTime > 0.0f) {
            float ratio = extrap_time / m_maxExtrapolationTime;
            decay = 1.0f / (1.0f + ratio * ratio);
        }

        float target_x = m_lastX;
        float target_y = m_lastY;
        float target_z = m_lastZ;

        if (m_hasVelocity) {
            target_x += m_velocityX * extrap_time * decay;
            target_y += m_velocityY * extrap_time * decay;
            target_z += m_velocityZ * extrap_time * decay;
        }

        // Exponentially decay the correction toward zero
        float corrBlend = std::exp(-kCorrectionSpeed * delta_time);
        m_correctionX *= corrBlend;
        m_correctionY *= corrBlend;
        m_correctionZ *= corrBlend;

        // Output = extrapolated target + diminishing correction
        m_outputX = target_x + m_correctionX;
        m_outputY = target_y + m_correctionY;
        m_outputZ = target_z + m_correctionZ;

        return PositionData(m_outputX, m_outputY, m_outputZ, raw.timestamp_us);
    }

    /// Resets all interpolation state.
    void Reset() {
        m_lastTimestampUs = 0;
        m_lastX = 0.0f;
        m_lastY = 0.0f;
        m_lastZ = 0.0f;
        m_velocityX = 0.0f;
        m_velocityY = 0.0f;
        m_velocityZ = 0.0f;
        m_hasVelocity = false;
        m_timeSinceLastSample = 0.0f;
        m_hasAnySample = false;
        m_correctionX = 0.0f;
        m_correctionY = 0.0f;
        m_correctionZ = 0.0f;
        m_outputX = 0.0f;
        m_outputY = 0.0f;
        m_outputZ = 0.0f;
        m_hasOutput = false;
    }

private:
    static constexpr float kVelocityBlend = 0.5f;
    static constexpr float kCorrectionSpeed = 300.0f;

    float m_maxExtrapolationTime = 0.1f;

    int64_t m_lastTimestampUs = 0;
    float m_lastX = 0.0f;
    float m_lastY = 0.0f;
    float m_lastZ = 0.0f;

    float m_velocityX = 0.0f;
    float m_velocityY = 0.0f;
    float m_velocityZ = 0.0f;
    bool m_hasVelocity = false;

    float m_timeSinceLastSample = 0.0f;
    bool m_hasAnySample = false;

    // Prediction-error correction
    float m_correctionX = 0.0f;
    float m_correctionY = 0.0f;
    float m_correctionZ = 0.0f;

    // Last returned output values
    float m_outputX = 0.0f;
    float m_outputY = 0.0f;
    float m_outputZ = 0.0f;
    bool m_hasOutput = false;
};

}  // namespace cameraunlock
