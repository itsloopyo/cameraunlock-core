#pragma once

#include "cameraunlock/math/angle_utils.h"

namespace cameraunlock {

/// Return type for PoseInterpolator â€” interpolated rotation values in degrees.
struct InterpolatedPose {
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
};

/// Fills in frames between tracking samples using linear interpolation.
/// Buffers one sample and lerps between the previous and current known positions,
/// trading one sample period of latency (16.7 ms at 60Hz, ~8 ms averaged over the
/// segment) for guaranteed smooth output at any display refresh rate.
///
/// Sits between the UDP receiver and tracking processor in the pipeline:
///   Raw Pose (60Hz) -> PoseInterpolator -> TrackingProcessor -> Camera
///
/// Port of CameraUnlock.Core.Processing.PoseInterpolator (C#).
class PoseInterpolator {
public:
    /// Maximum extrapolation past the target, as a fraction of the estimated
    /// sample interval. 0.5 = continue the last velocity for up to half a
    /// sample period beyond the latest known position.  This eliminates the
    /// velocity-drops-to-zero flat spots that make 60 Hz tracking look choppy
    /// on high-refresh displays (e.g. 240 Hz).
    float max_extrapolation_fraction = 0.5f;

    PoseInterpolator() = default;

    /// Seconds a sample may be late before the extrapolation starts expiring.
    /// Sized to outlast an ordinary Wi-Fi loss burst (50-200 ms), because a
    /// dropped packet or two is a live feed and must behave exactly as it did
    /// before: continue the prediction, then hold. Retreating on a dropped
    /// packet would pull the camera BACKWARDS against a head that is still
    /// turning, which reads far worse than the flat spot it replaces.
    static constexpr float kExtrapolationHoldSeconds = 0.25f;
    /// Seconds over which a genuinely stalled feed converges back to the last
    /// reported sample. Long enough that the correction is a drift, not a snap.
    static constexpr float kExtrapolationDecaySeconds = 0.35f;

    /// Segment position to sample at, given interpolation progress and how
    /// long the next sample has been outstanding.
    ///
    /// Progress past 1.0 is extrapolation: a short prediction that keeps
    /// velocity continuous between samples. It is only a prediction, so it
    /// must not outlive the sample it was predicting from by much. Clamping it
    /// and then HOLDING - the original behaviour - parks the output at 1.5x
    /// the last reported pose forever whenever new samples stop arriving: a
    /// tracker app streaming its last value while the face is lost, or a head
    /// so still that consecutive samples are bit-identical and the session's
    /// duplicate filter suppresses them. A 25 deg head turn then renders as
    /// 37.5 deg and stays there.
    ///
    /// So the prediction expires, but on a WALL CLOCK rather than on progress:
    /// progress is measured in units of an estimated sample interval, and that
    /// estimate is stale by construction in exactly the stall case (the EMA
    /// only updates when a new sample arrives). Below the hold threshold this
    /// is bit-for-bit the old behaviour; past it the segment position eases -
    /// smoothstep, so there is no velocity step at either end - to 1.0, which
    /// is the pose the tracker actually reported.
    float SegmentPosition(float progress, float time_since_last_sample) const {
        if (progress < 0.0f) return 0.0f;
        const float maxPt = 1.0f + max_extrapolation_fraction;
        const float pt = progress > maxPt ? maxPt : progress;
        if (time_since_last_sample <= kExtrapolationHoldSeconds) return pt;

        const float late = time_since_last_sample - kExtrapolationHoldSeconds;
        float u = late / kExtrapolationDecaySeconds;
        if (u > 1.0f) u = 1.0f;
        const float eased = u * u * (3.0f - 2.0f * u);  // smoothstep
        return pt + (1.0f - pt) * eased;
    }

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
                // Very first sample â€” park at this position
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

            // Capture current interpolated (possibly extrapolated) position as new start
            // point. Yaw and roll traverse the SHORTEST arc: they arrive in (-180, 180]
            // and can step across the seam, where a plain (to - from) turns a 1 degree
            // move from 179.5 to -179.5 into a -359 degree sweep the long way round.
            // Pitch is bounded to +/-90 by asin and cannot wrap. Matches C#
            // PoseInterpolator.
            const float t = SegmentPosition(m_progress, m_timeSinceLastNewSample);
            m_fromYaw   = math::NormalizeAngle(m_fromYaw + math::ShortestAngleDelta(m_fromYaw, m_toYaw) * t);
            m_fromPitch = m_fromPitch + (m_toPitch - m_fromPitch) * t;
            m_fromRoll  = math::NormalizeAngle(m_fromRoll + math::ShortestAngleDelta(m_fromRoll, m_toRoll) * t);

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

        // Allow extrapolation past 1.0 to maintain velocity continuity,
        // bounded to avoid runaway prediction on direction reversals.
        const float pt = SegmentPosition(m_progress, m_timeSinceLastNewSample);

        float outYaw   = math::NormalizeAngle(m_fromYaw + math::ShortestAngleDelta(m_fromYaw, m_toYaw) * pt);
        float outPitch = m_fromPitch + (m_toPitch - m_fromPitch) * pt;
        float outRoll  = math::NormalizeAngle(m_fromRoll + math::ShortestAngleDelta(m_fromRoll, m_toRoll) * pt);

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
    // Matches PoseInterpolator/PositionInterpolator in C#. Used only until the SECOND
    // sample arrives; at 1/60 a 30Hz tracker's first segment reached progress 1.0 in
    // half a sample period and extrapolated to the cap and back, a first-second-of-
    // session jolt that native mods had and Unity mods did not.
    static constexpr float kDefaultSampleInterval = 1.0f / 30.0f;
    // Bounds for sample interval estimate
    static constexpr float kMinSampleInterval = 0.001f;
    static constexpr float kMaxSampleInterval = 0.2f;

    // Interpolation segment: lerp from â†’ to
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

