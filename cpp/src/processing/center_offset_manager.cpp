#include "cameraunlock/processing/center_offset_manager.h"

namespace cameraunlock {

void CenterOffsetManager::SetCenter(const TrackingPose& pose) {
    SetCenter(pose.yaw, pose.pitch, pose.roll);
}

void CenterOffsetManager::SetCenter(float yaw, float pitch, float roll) {
    m_centerOffset = TrackingPose(yaw, pitch, roll, 0);
    m_centerQuaternionInverse = math::Quat4::FromYawPitchRoll(yaw, pitch, roll).Inverse();
    m_hasValidCenter = true;
}

void CenterOffsetManager::ApplyOffset(float& yaw, float& pitch, float& roll) const {
    if (!m_hasValidCenter) {
        return;
    }
    yaw -= m_centerOffset.yaw;
    pitch -= m_centerOffset.pitch;
    roll -= m_centerOffset.roll;
}

math::Quat4 CenterOffsetManager::ApplyOffsetQuat(const math::Quat4& input_q) const {
    if (!m_hasValidCenter) {
        return input_q;
    }
    return m_centerQuaternionInverse * input_q;
}

void CenterOffsetManager::ComposeAdditionalOffset(const math::Quat4& relative_q) {
    m_centerQuaternionInverse = relative_q.Inverse() * m_centerQuaternionInverse;
    float yaw, pitch, roll;
    m_centerQuaternionInverse.Inverse().ToEulerYXZ(yaw, pitch, roll);
    m_centerOffset = TrackingPose(yaw, pitch, roll, 0);
    m_hasValidCenter = true;
}

void CenterOffsetManager::Reset() {
    m_centerOffset = TrackingPose();
    m_centerQuaternionInverse = math::Quat4::Identity();
    m_hasValidCenter = false;
}

}  // namespace cameraunlock
