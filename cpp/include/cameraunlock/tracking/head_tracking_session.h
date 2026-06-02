#pragma once

#include "cameraunlock/data/position_data.h"
#include "cameraunlock/data/tracking_pose.h"
#include "cameraunlock/math/quat4.h"
#include "cameraunlock/math/vec3.h"
#include "cameraunlock/processing/pose_interpolator.h"
#include "cameraunlock/processing/position_interpolator.h"
#include "cameraunlock/processing/position_processor.h"
#include "cameraunlock/processing/tracking_processor.h"

#include <atomic>
#include <cstdint>

namespace cameraunlock {

/// Active tracking mode for a HeadTrackingSession.
enum class TrackingMode {
    RotationAndPosition = 0,
    RotationOnly = 1,
    PositionOnly = 2,
};

/// Complete per-frame head tracking pipeline:
///
///   rotation: receiver -> PoseInterpolator -> TrackingProcessor
///   position: receiver -> PositionInterpolator -> PositionProcessor
///
/// C++ port of CameraUnlock.Core.Tracking.HeadTrackingSession (C#), plus the
/// duplicate-packet filtering needed for trackers that resend identical
/// samples (e.g. a phone app sending at 60Hz off a 30Hz IMU): without it the
/// interpolator under-estimates the real sample interval and the inter-sample
/// frames it should be generating collapse into flat spots.
///
/// Call Update() once per render frame, then read GetRotation() /
/// GetPositionOffset() and apply them to the engine's camera however the mod
/// requires. The session does not own the receiver's lifecycle - the caller
/// starts and stops it.
///
/// TReceiver must provide:
///   bool GetRotation(float&, float&, float&) const
///   bool GetPosition(float&, float&, float&) const
///   int64_t GetLastReceiveTimestamp() const
///   void Recenter()
///
/// Unlike the C# session, recentering happens at the receiver level: the C++
/// TrackingProcessor's center manager overwrites rather than composes offsets,
/// so processor-level recentering only works for the first recenter.
///
/// Thread safety matches typical mod usage: Update() runs on the render
/// thread; SetMode()/CycleMode()/Recenter() may be called from a hotkey
/// thread. Mode changes are atomic; Recenter() races benignly with Update()
/// on float state, identical to the per-mod wiring it replaces.
template <typename TReceiver>
class HeadTrackingSession {
public:
    explicit HeadTrackingSession(TReceiver& receiver) : m_receiver(receiver) {}

    HeadTrackingSession(const HeadTrackingSession&) = delete;
    HeadTrackingSession& operator=(const HeadTrackingSession&) = delete;

    TrackingProcessor& GetProcessor() { return m_processor; }
    PositionProcessor& GetPositionProcessor() { return m_positionProcessor; }

    TrackingMode GetMode() const { return static_cast<TrackingMode>(m_mode.load()); }

    /// Switching position off resets position smoothing so re-enabling it
    /// does not blend from stale values.
    void SetMode(TrackingMode mode) {
        if (GetMode() == mode) return;
        m_mode.store(static_cast<int>(mode));
        if (!IsPositionActive()) {
            m_positionProcessor.ResetSmoothing();
            m_positionInterpolator.Reset();
        }
    }

    /// Advances to the next mode (6DOF -> rotation only -> position only -> 6DOF)
    /// and returns it.
    TrackingMode CycleMode() {
        SetMode(static_cast<TrackingMode>((m_mode.load() + 1) % 3));
        return GetMode();
    }

    bool IsRotationActive() const { return GetMode() != TrackingMode::PositionOnly; }
    bool IsPositionActive() const { return GetMode() != TrackingMode::RotationOnly; }

    /// Consecutive frames with tracker data required before the automatic
    /// first-connection recenter fires, giving phone trackers time to settle.
    void SetStabilizationFrames(int frames) { m_stabilizationFrames = frames; }
    int GetStabilizationFrames() const { return m_stabilizationFrames; }

    /// Runs the pipeline for this frame. Returns false when the receiver has
    /// no rotation data; cached outputs report invalid in that case.
    bool Update(float deltaTime) {
        float rawYaw, rawPitch, rawRoll;
        if (!m_receiver.GetRotation(rawYaw, rawPitch, rawRoll)) {
            m_rotationValid = false;
            m_positionValid = false;
            return false;
        }

        // Auto-recenter once tracking has stabilized after the first connection.
        if (!m_hasCentered) {
            m_stabilizationCount++;
            if (m_stabilizationCount >= m_stabilizationFrames) {
                m_hasCentered = true;
                Recenter();
            }
        }

        int64_t receiveTs = m_receiver.GetLastReceiveTimestamp();
        bool isNewPacket = (receiveTs != m_lastReceiveTimestamp);
        m_lastReceiveTimestamp = receiveTs;

        // Detect new DATA, not just new packets.
        bool isNewSample = isNewPacket &&
            (rawYaw != m_lastRawYaw || rawPitch != m_lastRawPitch || rawRoll != m_lastRawRoll);
        if (isNewPacket) {
            m_lastRawYaw = rawYaw;
            m_lastRawPitch = rawPitch;
            m_lastRawRoll = rawRoll;
        }

        m_lastRaw = TrackingPose(rawYaw, rawPitch, rawRoll);
        m_lastWasNewSample = isNewSample;

        m_lastInterpolated = m_poseInterpolator.Update(
            rawYaw, rawPitch, rawRoll, isNewSample, deltaTime);
        m_lastProcessed = m_processor.Process(
            m_lastInterpolated.yaw, m_lastInterpolated.pitch, m_lastInterpolated.roll, deltaTime);

        if (IsRotationActive()) {
            m_yaw = m_lastProcessed.yaw;
            m_pitch = m_lastProcessed.pitch;
            m_roll = m_lastProcessed.roll;
        } else {
            m_yaw = m_pitch = m_roll = 0.0f;
        }
        m_rotationValid = true;

        if (IsPositionActive()) {
            float rawX, rawY, rawZ;
            if (m_receiver.GetPosition(rawX, rawY, rawZ)) {
                PositionData rawPos(rawX, rawY, rawZ, receiveTs);
                PositionData interpolatedPos = m_positionInterpolator.Update(rawPos, deltaTime);

                math::Quat4 headRotQ = math::Quat4::FromYawPitchRoll(m_yaw, m_pitch, m_roll);

                math::Vec3 offset = m_positionProcessor.Process(interpolatedPos, headRotQ, deltaTime);
                m_posX = offset.x;
                m_posY = offset.y;
                m_posZ = offset.z;
                m_positionValid = true;
            } else {
                m_positionValid = false;
            }
        } else {
            m_posX = m_posY = m_posZ = 0.0f;
            m_positionValid = false;
        }

        return true;
    }

    /// Sets the current head pose and position as the new center and resets
    /// transient interpolation/smoothing state.
    void Recenter() {
        m_receiver.Recenter();
        m_processor.Reset();
        m_poseInterpolator.Reset();

        float px, py, pz;
        if (m_receiver.GetPosition(px, py, pz)) {
            m_positionProcessor.SetCenter(PositionData(px, py, pz));
        }
        m_positionInterpolator.Reset();
    }

    /// Processed rotation in degrees from the latest Update().
    /// Returns false (with zeros) when no rotation data is available.
    bool GetRotation(float& yaw, float& pitch, float& roll) const {
        if (!m_rotationValid) {
            yaw = pitch = roll = 0.0f;
            return false;
        }
        yaw = m_yaw;
        pitch = m_pitch;
        roll = m_roll;
        return true;
    }

    /// Processed position offset in meters from the latest Update().
    /// Returns false (with zeros) when position is unavailable or not in the mode.
    bool GetPositionOffset(float& x, float& y, float& z) const {
        if (!m_positionValid) {
            x = y = z = 0.0f;
            return false;
        }
        x = m_posX;
        y = m_posY;
        z = m_posZ;
        return true;
    }

    // Per-frame pipeline taps for diagnostics (raw -> interpolated -> processed).
    const TrackingPose& GetLastRaw() const { return m_lastRaw; }
    const InterpolatedPose& GetLastInterpolated() const { return m_lastInterpolated; }
    const TrackingPose& GetLastProcessed() const { return m_lastProcessed; }
    bool WasNewSample() const { return m_lastWasNewSample; }

private:
    TReceiver& m_receiver;
    PoseInterpolator m_poseInterpolator;
    TrackingProcessor m_processor;
    PositionInterpolator m_positionInterpolator;
    PositionProcessor m_positionProcessor;

    std::atomic<int> m_mode{static_cast<int>(TrackingMode::RotationAndPosition)};
    int m_stabilizationFrames = 30;
    int m_stabilizationCount = 0;
    bool m_hasCentered = false;

    int64_t m_lastReceiveTimestamp = 0;
    float m_lastRawYaw = 0.0f;
    float m_lastRawPitch = 0.0f;
    float m_lastRawRoll = 0.0f;

    float m_yaw = 0.0f, m_pitch = 0.0f, m_roll = 0.0f;
    bool m_rotationValid = false;

    float m_posX = 0.0f, m_posY = 0.0f, m_posZ = 0.0f;
    bool m_positionValid = false;

    TrackingPose m_lastRaw;
    InterpolatedPose m_lastInterpolated;
    TrackingPose m_lastProcessed;
    bool m_lastWasNewSample = false;
};

}  // namespace cameraunlock
