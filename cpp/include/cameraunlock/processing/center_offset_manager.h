#pragma once

#include "cameraunlock/data/tracking_pose.h"
#include "cameraunlock/math/quat4.h"

namespace cameraunlock {

/// Manages the center offset for head tracking recentering.
class CenterOffsetManager {
public:
    CenterOffsetManager() = default;

    /// The current center offset.
    const TrackingPose& GetCenterOffset() const { return m_centerOffset; }

    /// True if a center has been set.
    bool HasValidCenter() const { return m_hasValidCenter; }

    /// Sets the center offset to the specified pose.
    void SetCenter(const TrackingPose& pose);

    /// Sets the center offset using individual components.
    void SetCenter(float yaw, float pitch, float roll);

    /// Applies the offset by COMPONENT-WISE EULER SUBTRACTION.
    /// NOT equivalent to ApplyOffsetQuat for a compound centre: subtracting Euler
    /// components treats the three axes as independent, so a centre captured at a
    /// non-zero pitch leaves the yaw axis tilted and the result carries no roll where
    /// the true head-relative rotation has one. Retained for callers that genuinely
    /// want per-axis trim; the tracking pipeline uses ApplyOffsetQuat.
    void ApplyOffset(float& yaw, float& pitch, float& roll) const;

    /// Applies the offset in QUATERNION space: centerInverse * inputQ, i.e. the true
    /// head-relative rotation. This is what the pipeline uses, matching
    /// CenterOffsetManager.ApplyOffsetQuat in C#.
    math::Quat4 ApplyOffsetQuat(const math::Quat4& input_q) const;

    /// Composes an additional relative offset into the existing centre, used by
    /// Recenter() to fold the current smoothed rotation in. Accumulating in quaternion
    /// space is what makes a second recentre land correctly rather than leaving a
    /// residual equal to the previous centre.
    void ComposeAdditionalOffset(const math::Quat4& relative_q);

    /// Resets the center offset.
    void Reset();

private:
    TrackingPose m_centerOffset;
    math::Quat4 m_centerQuaternionInverse = math::Quat4::Identity();
    bool m_hasValidCenter = false;
};

}  // namespace cameraunlock
