#include "cameraunlock/processing/tracking_processor.h"
#include "cameraunlock/math/deadzone_utils.h"
#include "cameraunlock/math/smoothing_utils.h"
#include "cameraunlock/math/angle_utils.h"

namespace cameraunlock {

TrackingPose TrackingProcessor::Process(float yaw, float pitch, float roll, float delta_time) {
    // A port of TrackingProcessor.Process in C#, and it must stay step-for-step
    // identical: the two ports disagreed on BOTH the centring and the smoothing, so the
    // same tracker produced different camera motion in a native mod and a Unity mod -
    // and each port's comment asserted the opposite rationale to the other's.

    // Step 1: Convert raw Euler to quaternion and apply the centre offset there.
    // Quaternion composition, not per-axis Euler subtraction: with a centre captured at
    // a non-zero pitch, subtracting components leaves the yaw axis tilted and drops the
    // roll term the true head-relative rotation carries.
    math::Quat4 rawQ = math::Quat4::FromYawPitchRoll(yaw, pitch, roll);
    math::Quat4 centeredQ = m_centerManager.ApplyOffsetQuat(rawQ);

    // Step 2: Decompose to Euler for per-axis deadzone
    float cyaw, cpitch, croll;
    centeredQ.ToEulerYXZ(cyaw, cpitch, croll);

    cyaw = static_cast<float>(math::ApplyDeadzone(cyaw, m_deadzone.yaw));
    cpitch = static_cast<float>(math::ApplyDeadzone(cpitch, m_deadzone.pitch));
    croll = static_cast<float>(math::ApplyDeadzone(croll, m_deadzone.roll));

    // Step 3: Per-axis Euler smoothing, NOT quaternion SLERP. Slerp follows the great
    // circle between two orientations, and that arc's Euler decomposition carries a
    // non-zero roll term for compound movement - so diagonal head motion rolled the
    // horizon here while the C# port kept it at exactly zero. Yaw and roll take the
    // shortest arc because ToEulerYXZ returns them in (-180, 180] and they can step
    // across the seam; pitch is bounded to +/-90 by asin and cannot wrap.
    float effective_smoothing = static_cast<float>(math::GetEffectiveSmoothing(
        m_localSmoothing, m_remoteSmoothing, m_isRemoteConnection));

    if (!m_hasSmoothedValue) {
        m_smoothedYaw = cyaw;
        m_smoothedPitch = cpitch;
        m_smoothedRoll = croll;
        m_hasSmoothedValue = true;
    } else {
        m_smoothedYaw = math::SmoothAngle(m_smoothedYaw, cyaw, effective_smoothing, delta_time);
        m_smoothedPitch = math::Smooth(m_smoothedPitch, cpitch, effective_smoothing, delta_time);
        m_smoothedRoll = math::SmoothAngle(m_smoothedRoll, croll, effective_smoothing, delta_time);
    }

    // Step 4: Apply sensitivity
    float out_yaw = m_smoothedYaw * m_sensitivity.yaw;
    float out_pitch = m_smoothedPitch * m_sensitivity.pitch;
    float out_roll = m_smoothedRoll * m_sensitivity.roll;

    if (m_sensitivity.invert_yaw) out_yaw = -out_yaw;
    if (m_sensitivity.invert_pitch) out_pitch = -out_pitch;
    if (m_sensitivity.invert_roll) out_roll = -out_roll;

    return TrackingPose(out_yaw, out_pitch, out_roll);
}

void TrackingProcessor::Recenter() {
    // Composed onto the existing centre in QUATERNION space rather than replacing it.
    // Process() removes the centre before smoothing, so the smoothed value already has
    // it taken out; assigning that directly as the new centre works the first time and
    // leaves a residual equal to the previous centre on every later call. Any mod that
    // recentres automatically once and then offers a recentre hotkey hits it on the
    // user's first press. Matches TrackingProcessor.Recenter in C#.
    math::Quat4 smoothedQ =
        math::Quat4::FromYawPitchRoll(m_smoothedYaw, m_smoothedPitch, m_smoothedRoll);
    m_centerManager.ComposeAdditionalOffset(smoothedQ);

    // m_hasSmoothedValue is deliberately left SET, matching C#. Clearing it would send
    // the next Process() down the first-value branch and snap straight to the raw sample
    // instead of taking one smoothing step out of centre.
    //
    // Note this applies to a DIRECT call. HeadTrackingSession::Recenter centres at the
    // receiver and calls ResetSmoothing(), which does clear the flag - so the session's
    // recenter path snaps by design, because the receiver offset has already moved the
    // raw stream to centre and there is nothing to ease out of.
    m_smoothedYaw = 0.0f;
    m_smoothedPitch = 0.0f;
    m_smoothedRoll = 0.0f;
}

void TrackingProcessor::RecenterTo(float yaw, float pitch, float roll) {
    m_centerManager.SetCenter(yaw, pitch, roll);
    m_smoothedYaw = 0.0f;
    m_smoothedPitch = 0.0f;
    m_smoothedRoll = 0.0f;
}

void TrackingProcessor::ResetSmoothing() {
    m_smoothedYaw = 0.0f;
    m_smoothedPitch = 0.0f;
    m_smoothedRoll = 0.0f;
    m_hasSmoothedValue = false;
}

void TrackingProcessor::Reset() {
    m_centerManager.Reset();
    ResetSmoothing();
}

}  // namespace cameraunlock
