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

    static constexpr float kExtrapolationHoldSeconds = 0.25f;
    static constexpr float kExtrapolationDecaySeconds = 0.35f;

    /// Segment position to sample at, given progress and how long the next
    /// sample has been outstanding. Mirrors PoseInterpolator::SegmentPosition -
    /// see there for why the extrapolation expires on a wall clock rather than
    /// parking on the overshoot, and why a dropped packet must not trigger it.
    float SegmentPosition(float progress, float time_since_last_sample) const {
        if (progress < 0.0f) return 0.0f;
        const float maxPt = 1.0f + m_maxExtrapolationFraction;
        const float pt = progress > maxPt ? maxPt : progress;
        if (time_since_last_sample <= kExtrapolationHoldSeconds) return pt;

        const float late = time_since_last_sample - kExtrapolationHoldSeconds;
        float u = late / kExtrapolationDecaySeconds;
        if (u > 1.0f) u = 1.0f;
        const float eased = u * u * (3.0f - 2.0f * u);  // smoothstep
        return pt + (1.0f - pt) * eased;
    }

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
            const float t = SegmentPosition(m_progress, m_timeSinceLastNewSample);
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
        const float pt = SegmentPosition(m_progress, m_timeSinceLastNewSample);

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
