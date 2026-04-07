#include "cameraunlock/processing/tracking_processor.h"
#include "cameraunlock/math/deadzone_utils.h"
#include "cameraunlock/math/smoothing_utils.h"
#include "cameraunlock/math/angle_utils.h"

namespace cameraunlock {

TrackingPose TrackingProcessor::Process(float yaw, float pitch, float roll, float delta_time) {
    // Step 1: Apply center offset
    m_centerManager.ApplyOffset(yaw, pitch, roll);

    // Step 2: Apply deadzone
    yaw = static_cast<float>(math::ApplyDeadzone(yaw, m_deadzone.yaw));
    pitch = static_cast<float>(math::ApplyDeadzone(pitch, m_deadzone.pitch));
    roll = static_cast<float>(math::ApplyDeadzone(roll, m_deadzone.roll));

    // Step 3: Apply smoothing via quaternion SLERP.
    // SLERP follows the shortest arc on the unit sphere, avoiding the gimbal
    // artifacts that per-axis Euler smoothing can introduce at compound angles.
    double effective_smoothing = math::GetEffectiveSmoothing(m_smoothingFactor);

    math::Quat4 target = math::Quat4::FromYawPitchRoll(yaw, pitch, roll);

    if (!m_hasSmoothedValue) {
        m_smoothedQuat = target;
        m_hasSmoothedValue = true;
    } else {
        float t = static_cast<float>(
            math::CalculateSmoothingFactor(effective_smoothing, static_cast<double>(delta_time)));
        m_smoothedQuat = math::Quat4::Slerp(m_smoothedQuat, target, t);
    }

    // Decompose back to Euler for sensitivity application
    float smoothedYaw, smoothedPitch, smoothedRoll;
    m_smoothedQuat.ToEulerYXZ(smoothedYaw, smoothedPitch, smoothedRoll);

    // Step 4: Apply sensitivity
    float out_yaw = smoothedYaw * m_sensitivity.yaw;
    float out_pitch = smoothedPitch * m_sensitivity.pitch;
    float out_roll = smoothedRoll * m_sensitivity.roll;

    if (m_sensitivity.invert_yaw) out_yaw = -out_yaw;
    if (m_sensitivity.invert_pitch) out_pitch = -out_pitch;
    if (m_sensitivity.invert_roll) out_roll = -out_roll;

    return TrackingPose(out_yaw, out_pitch, out_roll);
}

void TrackingProcessor::Recenter() {
    float yaw, pitch, roll;
    m_smoothedQuat.ToEulerYXZ(yaw, pitch, roll);
    m_centerManager.SetCenter(yaw, pitch, roll);
}

void TrackingProcessor::RecenterTo(float yaw, float pitch, float roll) {
    m_centerManager.SetCenter(yaw, pitch, roll);
    m_smoothedQuat = math::Quat4::Identity();
    m_hasSmoothedValue = false;
}

void TrackingProcessor::Reset() {
    m_centerManager.Reset();
    m_smoothedQuat = math::Quat4::Identity();
    m_hasSmoothedValue = false;
}

}  // namespace cameraunlock
