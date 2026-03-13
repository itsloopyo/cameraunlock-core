#pragma once

#include "cameraunlock/data/position_data.h"
#include "cameraunlock/data/position_settings.h"
#include "cameraunlock/math/vec3.h"
#include "cameraunlock/math/quat4.h"
#include "cameraunlock/math/smoothing_utils.h"
#include "cameraunlock/math/angle_utils.h"
#include "cameraunlock/processing/neck_model.h"

namespace cameraunlock {

/// Complete positional tracking data processing pipeline.
/// Pipeline: raw position → center subtraction → tracker pivot compensation →
///           sensitivity/inversion → smoothing → add neck model → box clamp
/// Port of CameraUnlock.Core.Processing.PositionProcessor (C#).
class PositionProcessor {
public:
    PositionProcessor() = default;

    PositionSettings& GetSettings() { return m_settings; }
    const PositionSettings& GetSettings() const { return m_settings; }
    void SetSettings(const PositionSettings& settings) { m_settings = settings; }

    NeckModelSettings& GetNeckModelSettings() { return m_neckModelSettings; }
    const NeckModelSettings& GetNeckModelSettings() const { return m_neckModelSettings; }
    void SetNeckModelSettings(const NeckModelSettings& settings) { m_neckModelSettings = settings; }

    float GetTrackerPivotForward() const { return m_trackerPivotForward; }
    void SetTrackerPivotForward(float value) { m_trackerPivotForward = value; }

    /// Processes a raw position through the full pipeline.
    math::Vec3 Process(const PositionData& raw, const math::Quat4& processed_rotation_q,
                       float delta_time) {
        if (!raw.IsValid()) {
            return math::Vec3::Zero();
        }

        // Step 1: Center subtraction
        math::Vec3 pos = raw.ToVec3() - m_center;

        // Step 1.5: Subtract tracker pivot rotation artifact
        if (m_trackerPivotForward > 0.0f) {
            math::Vec3 pivot(0.0f, 0.0f, m_trackerPivotForward);
            math::Vec3 artifact = processed_rotation_q.Rotate(pivot) - pivot;
            pos = pos - artifact;
        }

        // Step 2: Per-axis sensitivity and inversion
        float x = pos.x * m_settings.sensitivity_x;
        float y = pos.y * m_settings.sensitivity_y;
        float z = pos.z * m_settings.sensitivity_z;

        if (m_settings.invert_x) x = -x;
        if (m_settings.invert_y) y = -y;
        if (m_settings.invert_z) z = -z;

        math::Vec3 scaled(x, y, z);

        // Step 3: Exponential smoothing on tracker position
        float effective_smoothing = static_cast<float>(
            math::GetEffectiveSmoothing(m_settings.smoothing));

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

        // Step 4: Add neck model offset
        math::Vec3 neck_offset = NeckModel::ComputeOffset(processed_rotation_q, m_neckModelSettings);
        math::Vec3 total = m_smoothedPosition + neck_offset;

        // Step 5: Box clamp total position against limits
        // Z uses asymmetric limits: positive Z = backward lean (restricted),
        // negative Z = forward lean (generous)
        math::Vec3 clamped(
            math::Clamp(total.x, -m_settings.limit_x, m_settings.limit_x),
            math::Clamp(total.y, -m_settings.limit_y, m_settings.limit_y),
            math::Clamp(total.z, -m_settings.limit_z, m_settings.limit_z_back)
        );

        return clamped;
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
    NeckModelSettings m_neckModelSettings;
    float m_trackerPivotForward = 0.15f;

    math::Vec3 m_center;
    math::Vec3 m_smoothedPosition;
    bool m_hasSmoothedValue = false;
};

}  // namespace cameraunlock
