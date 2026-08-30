#pragma once

#include "cameraunlock/data/position_data.h"
#include "cameraunlock/data/position_settings.h"
#include "cameraunlock/math/vec3.h"
#include "cameraunlock/math/quat4.h"
#include "cameraunlock/math/smoothing_utils.h"
#include "cameraunlock/math/angle_utils.h"

namespace cameraunlock {

/// Complete positional tracking data processing pipeline.
/// Pipeline: raw position → center subtraction → tracker pivot compensation →
///           sensitivity/inversion → smoothing → box clamp
/// Port of CameraUnlock.Core.Processing.PositionProcessor (C#).
class PositionProcessor {
public:
    PositionProcessor() = default;

    PositionSettings& GetSettings() { return m_settings; }
    const PositionSettings& GetSettings() const { return m_settings; }
    void SetSettings(const PositionSettings& settings) { m_settings = settings; }

    float GetTrackerPivotForward() const { return m_trackerPivotForward; }
    void SetTrackerPivotForward(float value) { m_trackerPivotForward = value; }

    /// Upward distance from the neck pivot to the point the tracker watches, the vertical
    /// companion to the forward arm. A tracker watching the eyes sees a point both ahead of
    /// and above the neck, and pitching the head swings it through both. Defaults to 0, and
    /// compensation stays off until one of the two is positive.
    float GetTrackerPivotUp() const { return m_trackerPivotUp; }
    void SetTrackerPivotUp(float value) { m_trackerPivotUp = value; }

    /// Fed from the receiver's IsRemoteConnection() every update so a switch
    /// between a local tracker and a remote one picks up the other parameter
    /// without a restart. Runtime state, deliberately not part of PositionSettings.
    void SetIsRemoteConnection(bool is_remote) { m_isRemoteConnection = is_remote; }

    bool IsRemoteConnection() const { return m_isRemoteConnection; }

    /// Processes a raw position through the full pipeline.
    /// physical_rotation_q is the centered head rotation BEFORE per-axis sensitivity
    /// and inversion. The pivot artifact is a physical property of where the tracker's
    /// face point sits relative to the neck, so scaling the angle first over-corrects
    /// by the sensitivity factor and inverting it drives the correction backwards.
    math::Vec3 Process(const PositionData& raw, const math::Quat4& physical_rotation_q,
                       float delta_time) {
        if (!raw.IsValid()) {
            return math::Vec3::Zero();
        }

        // Step 1: Center subtraction
        math::Vec3 pos = raw.ToVec3() - m_center;

        // Step 1.5: Subtract tracker pivot rotation artifact.
        // The pivot vector is -z, not +z: negative z is forward throughout this library
        // and the Headcam trackers pin the same convention with a test ("wire +Z out the
        // back of the head"). Rotation is linear, so R(-v) - (-v) == -(R(v) - v): a +z
        // pivot computed the exact NEGATION of the real artifact and the subtraction then
        // DOUBLED the phantom translation it was supposed to remove.
        if (m_trackerPivotForward > 0.0f || m_trackerPivotUp > 0.0f) {
            math::Vec3 pivot(0.0f, m_trackerPivotUp, -m_trackerPivotForward);
            math::Vec3 artifact = physical_rotation_q.Rotate(pivot) - pivot;
            pos = pos - artifact;
        }

        // Step 2: Per-axis sensitivity and inversion
        float x = pos.x * m_settings.sensitivity_x;
        float y = pos.y * m_settings.sensitivity_y;
        float z = pos.z * m_settings.sensitivity_z;

        // Inversion is a TRACKER-axis correction and belongs here, ahead of the
        // clamp: it puts a stream whose axis runs the other way back into the
        // pipeline's convention, and the clamp below is written in that
        // convention. An engine whose camera-local +z is forward must NOT be
        // handled with invert_z - the flip belongs in the code that hands the
        // offset to the engine, after the clamp, or the forward lean is clamped
        // on the backward budget. docs/porting-the-pipeline.md section 11.
        if (m_settings.invert_x) x = -x;
        if (m_settings.invert_y) y = -y;
        if (m_settings.invert_z) z = -z;

        // Clamped BEFORE smoothing as well as after: the smoothing state used to track
        // the unclamped input, so a high sensitivity drove it far outside the limits and
        // it then sat saturated on the way back, pinning the output at the limit for
        // hundreds of milliseconds after the head had returned. Clamping a bounded input
        // is a no-op, so nothing changes for the common case.
        math::Vec3 scaled = ClampToLimits(math::Vec3(x, y, z));

        // Step 3: Exponential smoothing on tracker position
        float effective_smoothing = static_cast<float>(
            math::GetEffectiveSmoothing(m_settings.local_smoothing,
                                        m_settings.remote_smoothing,
                                        m_isRemoteConnection));

        if (!m_hasSmoothedValue) {
            m_smoothedPosition = scaled;
            m_hasSmoothedValue = true;
        } else {
            float t = math::CalculateSmoothingFactor(effective_smoothing, delta_time);
            m_smoothedPosition = math::Vec3(
                math::Lerp(m_smoothedPosition.x, scaled.x, t),
                math::Lerp(m_smoothedPosition.y, scaled.y, t),
                math::Lerp(m_smoothedPosition.z, scaled.z, t)
            );
        }

        // Step 4: Box clamp position against limits
        return ClampToLimits(m_smoothedPosition);
    }

    /// Y and Z are asymmetric: positive Z = backward lean (restricted), negative Z =
    /// forward lean (generous); positive Y = up (generous), negative Y = down
    /// (restricted, to avoid clipping into the player body).
    math::Vec3 ClampToLimits(const math::Vec3& value) const {
        return math::Vec3(
            math::Clamp(value.x, -m_settings.limit_x, m_settings.limit_x),
            math::Clamp(value.y, -m_settings.limit_y_down, m_settings.limit_y),
            math::Clamp(value.z, -m_settings.limit_z, m_settings.limit_z_back)
        );
    }

    /// Sets the center offset for recentering.
    void SetCenter(const PositionData& center_position) {
        m_center = center_position.ToVec3();
    }

    /// Resets only the smoothing state, preserving center offset.
    void ResetSmoothing() {
        m_smoothedPosition = math::Vec3::Zero();
        m_hasSmoothedValue = false;
    }

    /// Resets the processor state.
    void Reset() {
        m_center = math::Vec3::Zero();
        m_smoothedPosition = math::Vec3::Zero();
        m_hasSmoothedValue = false;
    }

private:
    PositionSettings m_settings;
    /// Defaults to 0 - compensation OFF - and must be measured per tracker app, because
    /// the correct arm length is not a property of this library: the Headcam Android app
    /// already applies its own eye-anchor offset while iOS applies none, so one shared
    /// constant is wrong for at least one of them. The previous defaults (0.15 here,
    /// 0.01 in C#) were both chosen while the compensation was inverted and doubling the
    /// artifact it removed, so neither carries over. Matches C# PositionProcessor.
    float m_trackerPivotForward = 0.0f;
    float m_trackerPivotUp = 0.0f;
    bool m_isRemoteConnection = false;

    math::Vec3 m_center;
    math::Vec3 m_smoothedPosition;
    bool m_hasSmoothedValue = false;
};

}  // namespace cameraunlock
