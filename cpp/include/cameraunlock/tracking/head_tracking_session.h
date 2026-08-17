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
#include <cmath>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace cameraunlock {

namespace detail {
template <typename T, typename = void>
struct HasRecenterRequest : std::false_type {};
template <typename T>
struct HasRecenterRequest<T, std::void_t<decltype(std::declval<T&>().TryConsumeRecenterRequest())>>
    : std::true_type {};

// Detected through a NON-const T&, matching HasRecenterRequest above. A const T&
// probe would reject an adapter whose IsRemoteConnection() merely forgot the
// const qualifier, and it would reject it silently: detection failing just
// compiles the propagation block away, so the session reports local forever and
// every remote user gets the local smoothing value with no diagnostic anywhere.
// The non-const form accepts both spellings.
template <typename T, typename = void>
struct HasRemoteConnection : std::false_type {};
template <typename T>
struct HasRemoteConnection<T, std::void_t<decltype(std::declval<T&>().IsRemoteConnection())>>
    : std::true_type {};
}  // namespace detail

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
/// and may provide (detected at compile time):
///   bool TryConsumeRecenterRequest()
/// When present, Update() recenters whenever the tracker app signals CENTER
/// through the packet trailer. When absent the whole remote-recenter branch
/// compiles away with no diagnostic - a receiver ADAPTER that wraps a core
/// receiver but forgets to forward TryConsumeRecenterRequest() silently loses
/// tracker-app recentering. Adapters should assert against it:
///   static_assert(MySession::kHasRemoteRecenter);
///
/// Unlike the C# session, recentering happens at the receiver level: the C++
/// TrackingProcessor's center manager overwrites rather than composes offsets,
/// so processor-level recentering only works for the first recenter. This is
/// also what makes the remote recenter immune to double subtraction: the
/// tracker app zeroes its own output before signaling, and Recenter() captures
/// the receiver's raw (already-zeroed) pose as the offset rather than folding
/// the previous smoothed pose in a second time. A refactor to processor-level
/// centering must preserve that (see TestRecenterSeedsCurrentFrameWithCenteredPose).
///
/// Thread safety matches typical mod usage: Update() runs on the render
/// thread; SetMode()/CycleMode()/Recenter() may be called from a hotkey
/// thread. Mode changes are atomic; Recenter() races benignly with Update()
/// on float state, identical to the per-mod wiring it replaces.
template <typename TReceiver>
class HeadTrackingSession {
public:
    /// True when TReceiver exposes TryConsumeRecenterRequest() and remote
    /// (tracker-app) recentering is active for this instantiation.
    static constexpr bool kHasRemoteRecenter = detail::HasRecenterRequest<TReceiver>::value;

    /// True when TReceiver exposes IsRemoteConnection() and Update() can select
    /// between the local and remote smoothing parameters itself. When absent the
    /// session always reports a local connection and the mod must call
    /// GetProcessor().SetIsRemoteConnection() itself.
    static constexpr bool kHasRemoteConnection = detail::HasRemoteConnection<TReceiver>::value;

    explicit HeadTrackingSession(TReceiver& receiver) : m_receiver(receiver) { PushSmoothing(); }

    HeadTrackingSession(const HeadTrackingSession&) = delete;
    HeadTrackingSession& operator=(const HeadTrackingSession&) = delete;

    TrackingProcessor& GetProcessor() { return m_processor; }

    /// Prefer SetPositionSettings() over GetPositionProcessor().SetSettings(): the
    /// session owns the two smoothing values and a raw SetSettings would carry the
    /// struct's own smoothing fields instead. Update() re-asserts the owned pair, so
    /// a raw assignment is corrected on the next frame rather than persisting.
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

    /// Smoothing applied when the tracker runs on this machine (loopback).
    /// Forwarded to both the rotation and the position processor.
    void SetLocalSmoothing(float smoothing) {
        m_localSmoothing = smoothing;
        PushSmoothing();
    }

    /// Smoothing applied when the tracker is a remote device on the network.
    /// Forwarded to both the rotation and the position processor.
    void SetRemoteSmoothing(float smoothing) {
        m_remoteSmoothing = smoothing;
        PushSmoothing();
    }

    /// The session's own smoothing values, which is what both processors are running.
    float GetLocalSmoothing() const { return m_localSmoothing; }
    float GetRemoteSmoothing() const { return m_remoteSmoothing; }

    /// Position settings for this session. The session's two smoothing values are
    /// recomposed onto the incoming struct, so assigning settings can never undo a
    /// SetLocalSmoothing()/SetRemoteSmoothing() call and the two compose in either
    /// order. The smoothing fields carried by @p settings are ignored.
    void SetPositionSettings(const PositionSettings& settings) {
        PositionSettings next = settings;
        next.local_smoothing = m_localSmoothing;
        next.remote_smoothing = m_remoteSmoothing;
        m_positionProcessor.SetSettings(next);
    }

    const PositionSettings& GetPositionSettings() const {
        return m_positionProcessor.GetSettings();
    }

    /// Whether the latest Update() saw a remote connection.
    bool IsRemoteConnection() const { return m_isRemoteConnection; }

    /// Consecutive settled packets required before the automatic
    /// first-connection recenter fires, giving phone trackers time to settle.
    void SetStabilizationFrames(int frames) { m_stabilizationFrames = frames; }
    int GetStabilizationFrames() const { return m_stabilizationFrames; }

    /// How far past the latest sample the interpolators may continue the last
    /// velocity, as a fraction of the estimated sample interval. Applies to
    /// rotation and position together.
    ///
    /// The default 0.5 fills the flat spots when a slow tracker drives a fast
    /// display, but it does so by OVERSHOOTING the newest known pose and then
    /// holding there until the next sample arrives. When the render rate is far
    /// above the sample rate, each sample period becomes: interpolate, overshoot,
    /// freeze, then correct back on the next sample - a wobble at the tracker's
    /// sample rate that shows up in every camera mode. Set to 0 to interpolate
    /// only between known samples.
    void SetMaxExtrapolationFraction(float fraction) {
        m_poseInterpolator.max_extrapolation_fraction = fraction;
        m_positionInterpolator.SetMaxExtrapolationFraction(fraction);
    }
    float GetMaxExtrapolationFraction() const {
        return m_poseInterpolator.max_extrapolation_fraction;
    }

    /// True once a center has been captured, automatically or on request.
    bool HasCentered() const { return m_hasCentered; }

    /// How many times the tracker app has recentered us through the packet
    /// trailer. Compare across frames to report a press: the session recenters
    /// itself inside Update(), so without this a mod cannot say whether the
    /// player's CENTER press reached the game, and "I pressed it and nothing
    /// happened" has no evidence either way.
    uint64_t GetRemoteRecenterCount() const { return m_remoteRecenters; }

    /// Runs the pipeline for this frame. Returns false when the receiver has
    /// no rotation data; cached outputs report invalid in that case.
    bool Update(float deltaTime) {
        float rawYaw, rawPitch, rawRoll;
        if (!m_receiver.GetRotation(rawYaw, rawPitch, rawRoll)) {
            m_rotationValid = false;
            m_positionValid = false;
            return false;
        }

        // Re-assert rather than trust: GetPositionProcessor() hands the caller a
        // reference, so a SetSettings() made straight on the processor would otherwise
        // carry that struct's smoothing fields and quietly displace the session's.
        PushSmoothing();

        // Re-read every frame: the smoothing parameter is selected per connection,
        // so a switch from a local tracker to a remote one must take effect without
        // a restart.
        if constexpr (detail::HasRemoteConnection<TReceiver>::value) {
            m_isRemoteConnection = m_receiver.IsRemoteConnection();
            m_processor.SetIsRemoteConnection(m_isRemoteConnection);
            m_positionProcessor.SetIsRemoteConnection(m_isRemoteConnection);
        }

        const int64_t receiveTs = m_receiver.GetLastReceiveTimestamp();
        const bool isNewPacket = (receiveTs != m_lastReceiveTimestamp);
        m_lastReceiveTimestamp = receiveTs;

        // Auto-recenter once, on the first pose the player HOLDS still after the
        // first connection.
        //
        // Elapsed frames alone say nothing about whether the player is in
        // position: a mod starts tracking while the game is still on its intro
        // screens, so a plain countdown captures whatever pose they happened to
        // hold half a minute before they sit down, and that offset then rides
        // the view for the rest of the session. A held pose is the moment they
        // have settled in front of the monitor.
        //
        // The window counts PACKETS, not frames. Receivers report their last
        // known pose whether or not it is fresh, so a tracker that has stopped
        // sending reads as a perfectly still head - the one state that must
        // never be mistaken for a settled player.
        bool recentered = false;
        if (!m_hasCentered && isNewPacket) {
            if (!m_hasSettleAnchor || fabsf(rawYaw - m_settleYaw) > kSettleDegrees ||
                fabsf(rawPitch - m_settlePitch) > kSettleDegrees ||
                fabsf(rawRoll - m_settleRoll) > kSettleDegrees) {
                m_settleYaw = rawYaw;
                m_settlePitch = rawPitch;
                m_settleRoll = rawRoll;
                m_hasSettleAnchor = true;
                m_stabilizationCount = 0;
            } else if (++m_stabilizationCount >= m_stabilizationFrames) {
                m_hasCentered = true;
                Recenter();
                recentered = true;
            }
        }

        if constexpr (detail::HasRecenterRequest<TReceiver>::value) {
            if (m_receiver.TryConsumeRecenterRequest()) {
                m_hasCentered = true;
                Recenter();
                recentered = true;
                ++m_remoteRecenters;
            }
        }

        if (recentered) {
            // Recenter() changed the receiver's offset; re-fetch so this
            // frame's pipeline is seeded with the centered pose (matching the
            // C# session, which consumes before fetching). Feeding the stale
            // pre-recenter values into the freshly reset interpolator would
            // hold the old orientation for a packet interval.
            m_receiver.GetRotation(rawYaw, rawPitch, rawRoll);
        }

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

                // Same duplicate-sample filter the rotation path here uses. receiveTs
                // advances on every datagram, so a phone resending at 60Hz off a 30Hz
                // sensor made the position interpolator estimate half the true sample
                // interval while rotation estimated it correctly - position then reached
                // the extrapolation cap halfway through every sample period and wobbled at
                // 30Hz while the head rotation stayed smooth.
                //
                // This is one place the two ports deliberately DIVERGE, and the reason is
                // where each centres. This port centres at the RECEIVER, so a recenter
                // changes the raw values it reports and a Reset interpolator re-seeds on
                // the next packet. The C# port centres at the PROCESSOR, leaving the raw
                // stream untouched by a recenter - so the same filter would stall a
                // re-Reset interpolator indefinitely for a user holding perfectly still,
                // because no value would ever change to re-seed it. C# therefore keeps
                // timestamp-only detection on both channels.
                bool isNewPosSample = isNewPacket &&
                    (rawX != m_lastRawPosX || rawY != m_lastRawPosY || rawZ != m_lastRawPosZ);
                if (isNewPacket) {
                    m_lastRawPosX = rawX;
                    m_lastRawPosY = rawY;
                    m_lastRawPosZ = rawZ;
                }

                PositionData interpolatedPos =
                    m_positionInterpolator.Update(rawPos, isNewPosSample, deltaTime);

                // The PHYSICAL head rotation, taken from the processor's smoothed state
                // rather than from m_yaw/m_pitch/m_roll. Those carry per-axis sensitivity
                // and inversion, and in PositionOnly mode they are forced to zero - so the
                // pivot quaternion became identity and no compensation was applied at all,
                // while the C# port applied the full term. Matches HeadTrackingSession.cs.
                float physYaw, physPitch, physRoll;
                m_processor.GetSmoothedRotation(physYaw, physPitch, physRoll);
                math::Quat4 physicalRotQ = math::Quat4::FromYawPitchRoll(physYaw, physPitch, physRoll);

                math::Vec3 offset = m_positionProcessor.Process(interpolatedPos, physicalRotQ, deltaTime);
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
    ///
    /// Also disarms the automatic recenter. A deliberate recenter is the
    /// definitive answer to where centre is, and leaving the automatic one armed
    /// means it fires the moment the player next holds still for long enough and
    /// silently replaces the centre they just chose - the same trap the remote
    /// recenter path already guards against.
    void Recenter() {
        m_hasCentered = true;
        m_receiver.Recenter();

        // ResetSmoothing, not Reset. Reset() also clears the processor's centre offset,
        // so any mod-configured correction applied through
        // GetProcessor().GetCenterManager().SetCenter(...) - the documented way to trim a
        // phone sitting a few degrees off-axis - was wiped by every automatic and remote
        // recenter, and the player had to re-apply it each time the tracker fired
        // DEVICE_MOVED. Centring here happens at the receiver level, so the processor
        // only needs its transient smoothing cleared.
        m_processor.ResetSmoothing();
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
    // The session, not PositionSettings, owns the two smoothing values. They still live
    // in PositionSettings (that is the shape mods are wired to), but a settings struct is
    // assigned wholesale, so a SetSettings after a SetLocalSmoothing would otherwise reset
    // position smoothing to the struct's defaults while rotation smoothing kept the value
    // the user asked for.
    void PushSmoothing() {
        m_processor.SetLocalSmoothing(m_localSmoothing);
        m_processor.SetRemoteSmoothing(m_remoteSmoothing);
        m_positionProcessor.GetSettings().local_smoothing = m_localSmoothing;
        m_positionProcessor.GetSettings().remote_smoothing = m_remoteSmoothing;
    }

    TReceiver& m_receiver;
    PoseInterpolator m_poseInterpolator;
    TrackingProcessor m_processor;
    PositionInterpolator m_positionInterpolator;
    PositionProcessor m_positionProcessor;

    std::atomic<int> m_mode{static_cast<int>(TrackingMode::RotationAndPosition)};
    // How far the pose may wander, per axis, and still count as held. Wide
    // enough to ride out tracker jitter, narrow enough that reaching for the
    // mouse or glancing at the keyboard restarts the window.
    static constexpr float kSettleDegrees = 1.5f;

    int m_stabilizationFrames = 30;
    int m_stabilizationCount = 0;
    float m_settleYaw = 0.0f, m_settlePitch = 0.0f, m_settleRoll = 0.0f;
    bool m_hasSettleAnchor = false;
    bool m_hasCentered = false;
    uint64_t m_remoteRecenters = 0;
    bool m_isRemoteConnection = false;

    float m_localSmoothing = static_cast<float>(math::kDefaultLocalSmoothing);
    float m_remoteSmoothing = static_cast<float>(math::kDefaultRemoteSmoothing);

    int64_t m_lastReceiveTimestamp = 0;
    float m_lastRawYaw = 0.0f;
    float m_lastRawPitch = 0.0f;
    float m_lastRawRoll = 0.0f;
    float m_lastRawPosX = 0.0f;
    float m_lastRawPosY = 0.0f;
    float m_lastRawPosZ = 0.0f;

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
