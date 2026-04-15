#pragma once

#include "cameraunlock/data/position_data.h"

namespace cameraunlock {

/// Fills in frames between position samples using linear interpolation.
/// Same algorithm as PoseInterpolator but for 3D position (x, y, z).
/// Port of CameraUnlock.Core.Processing.PositionInterpolator (C#).
class PositionInterpolator {
public:
    PositionInterpolator() = default;

    float GetMaxExtrapolationFraction() const { return m_maxExtrapolationFraction; }
    void SetMaxExtrapolationFraction(float value) { m_maxExtrapolationFraction = value; }

    /// Update with the latest raw position and frame delta time.
    /// Returns a smoothly interpolated position.
    PositionData Update(const PositionData& raw, float delta_time) {
        if (!raw.IsValid()) {
            return raw;
        }

        m_timeSinceLastNewSample += delta_time;

        bool is_new_sample = raw.timestamp_us != m_lastTimestampUs;

        if (is_new_sample) {
            if (!m_hasFirstSample) {
                m_fromX = raw.x;  m_fromY = raw.y;  m_fromZ = raw.z;
                m_toX = raw.x;    m_toY = raw.y;    m_toZ = raw.z;
                m_lastTimestampUs = raw.timestamp_us;
                m_progress = 1.0f;
                m_timeSinceLastNewSample = 0.0f;
                m_hasFirstSample = true;
                return raw;
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

            // Capture current interpolated (possibly extrapolated) position as new start point
            float maxP = 1.0f + m_maxExtrapolationFraction;
            float t = m_progress < 0.0f ? 0.0f : (m_progress > maxP ? maxP : m_progress);
            m_fromX = m_fromX + (m_toX - m_fromX) * t;
            m_fromY = m_fromY + (m_toY - m_fromY) * t;
            m_fromZ = m_fromZ + (m_toZ - m_fromZ) * t;

            // New sample becomes the target
            m_toX = raw.x;  m_toY = raw.y;  m_toZ = raw.z;
            m_lastTimestampUs = raw.timestamp_us;
            m_progress = 0.0f;
            m_timeSinceLastNewSample = 0.0f;
        }

        if (!m_hasFirstSample) {
            return raw;
        }

        // Advance interpolation
        m_progress += delta_time / m_sampleInterval;

        // Allow extrapolation past 1.0 to maintain velocity continuity
        float maxPt = 1.0f + m_maxExtrapolationFraction;
        float pt = m_progress > maxPt ? maxPt : (m_progress < 0.0f ? 0.0f : m_progress);

        float outX = m_fromX + (m_toX - m_fromX) * pt;
        float outY = m_fromY + (m_toY - m_fromY) * pt;
        float outZ = m_fromZ + (m_toZ - m_fromZ) * pt;

        return PositionData(outX, outY, outZ, raw.timestamp_us);
    }

    /// Resets all interpolation state.
    void Reset() {
        m_lastTimestampUs = 0;
        m_fromX = 0.0f;  m_fromY = 0.0f;  m_fromZ = 0.0f;
        m_toX = 0.0f;    m_toY = 0.0f;    m_toZ = 0.0f;
        m_progress = 0.0f;
        m_sampleInterval = kDefaultSampleInterval;
        m_timeSinceLastNewSample = 0.0f;
        m_hasFirstSample = false;
        m_hasSecondSample = false;
    }

private:
    static constexpr float kIntervalBlend = 0.3f;
    static constexpr float kDefaultSampleInterval = 1.0f / 60.0f;
    static constexpr float kMinSampleInterval = 0.001f;
    static constexpr float kMaxSampleInterval = 0.2f;

    float m_maxExtrapolationFraction = 0.5f;

    int64_t m_lastTimestampUs = 0;

    float m_fromX = 0.0f, m_fromY = 0.0f, m_fromZ = 0.0f;
    float m_toX = 0.0f, m_toY = 0.0f, m_toZ = 0.0f;

    float m_progress = 0.0f;
    float m_sampleInterval = kDefaultSampleInterval;
    float m_timeSinceLastNewSample = 0.0f;

    bool m_hasFirstSample = false;
    bool m_hasSecondSample = false;
};

}  // namespace cameraunlock
