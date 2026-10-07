#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <thread>
#include <cstdint>
#include "cameraunlock/data/tracking_pose.h"
#include "cameraunlock/protocol/pose_jump_gate.h"
#include "cameraunlock/protocol/socket_types.h"
#include "cameraunlock/protocol/udp_socket.h"

namespace cameraunlock {
/// Routes receiver diagnostics to cameraunlock::logging by default.
std::function<void(const std::string&)> DefaultLogSink();
}  // namespace cameraunlock

namespace cameraunlock {

class UdpReceiver;

namespace detail {

/// Not API: the test seam. Hands a receiver that is not started one datagram as if it had
/// arrived from `sender` at `arrivedUs`, on the caller's thread, so a test can run the
/// receive path without a socket or a clock.
struct UdpReceiverTestAccess {
    static void Deliver(UdpReceiver& receiver, const void* datagram, int length, const sockaddr_in& sender,
                        int64_t arrivedUs);
};

}  // namespace detail

/// UDP receiver for OpenTrack protocol.
/// Thread-safe with lock-free reads on the game thread.
class UdpReceiver {
public:
    /// Default OpenTrack UDP port.
    static constexpr uint16_t kDefaultPort = 4242;

    /// Connection timeout in milliseconds.
    /// Lower than PollingUdpReceiver (500 vs 1000) because the threaded receiver
    /// checks more frequently and can detect disconnects sooner.
    static constexpr int kConnectionTimeoutMs = 500;

    // Separate from the connection-liveness timeout above. Re-arming trailer
    // first-sighting is a wire-contract rule fixed at ~5s of packet silence (see
    // AGENTS.md and OpenTrackReceiver.cs, which implements 50 x 100ms). Reusing the
    // 500ms liveness value meant a routine Wi-Fi stall inside a recenter burst re-armed
    // mid-burst, so the burst's tail - carrying the SAME counter - read as a second
    // press and recentred on whatever pose the head had drifted to.
    static constexpr int kRecenterRearmMs = 5000;
    /// How long the chosen tracker may go quiet before another sender is
    /// allowed to take over. Long enough that ordinary jitter or a dropped
    /// packet never hands control to a second app mid-session.
    static constexpr int kSourceHandoverMs = 2000;

    /// The gate's threshold, PoseJumpGate::kConfirmJumpDegrees.
    static constexpr float kConfirmJumpDegrees = PoseJumpGate::kConfirmJumpDegrees;

    /// Interval between bind retries after a failed bind.
    /// Short on purpose: this is the only path that reclaims the port, so it
    /// covers both a slow-exiting previous game instance (sub-second) and a
    /// tracker app the user quits mid-session, which should be picked up
    /// promptly rather than feeling broken.
    static constexpr int kRetryIntervalMs = 500;

    /// Interval between "still waiting" retry log messages.
    static constexpr int kRetryLogIntervalMs = 30000;

    UdpReceiver() = default;
    ~UdpReceiver();

    // Non-copyable
    UdpReceiver(const UdpReceiver&) = delete;
    UdpReceiver& operator=(const UdpReceiver&) = delete;

    /// Starts the UDP receiver on the specified port and a supervisor thread
    /// that keeps it listening for as long as the receiver lives. If the bind
    /// fails - the port is already in use being the usual reason - Start
    /// returns false, logs what the OS said, and the supervisor retries every
    /// kRetryIntervalMs until it succeeds, with no further action from the
    /// caller; once it binds, the receive thread starts and IsRunning becomes
    /// true. The supervisor also re-establishes the socket if the receive
    /// thread dies on a socket error.
    /// @return True if bound and the receive thread started immediately.
    bool Start(uint16_t port = kDefaultPort);

    /// Stops the UDP receiver. Joins the supervisor and receive threads,
    /// closes the socket, and clears tracking state.
    void Stop();

    /// Optional logging callback for bind failures and retry messages.
    /// Must be thread-safe: invoked from Start (caller thread) and from the
    /// background retry thread.
    /// Diagnostic sink. Defaults to the shared file log rather than to nothing:
    /// forgetting this call used to silently discard the bind result and the
    /// latched first-packet line, which are the two the "no head tracking"
    /// reports turn on. Mods with their own logger override it here; mods that
    /// never open a core log get a no-op.
    void SetLog(std::function<void(const std::string&)> log) { m_log = std::move(log); }

    /// True if the receive thread is running.
    bool IsRunning() const { return m_running.load(std::memory_order_acquire); }

    /// True if the supervisor is retrying the bind (the last attempt failed).
    bool IsRetrying() const { return m_retrying.load(std::memory_order_acquire); }

    /// True if data has been received recently.
    bool IsReceiving() const;

    /// True if the data source is from a remote address.
    bool IsRemoteConnection() const { return m_isRemoteConnection.load(std::memory_order_relaxed); }

    /// True if the most recent bind attempt failed. Cleared once retry succeeds.
    bool IsFailed() const { return m_failed.load(std::memory_order_acquire); }

    /// What the OS said when Start() could not bind, and empty when it bound. For a host
    /// that gives the port up where a mod waits for it: a launcher that leaves head tracking
    /// to the program already listening calls Stop() and says why. Start() writes it before
    /// the supervisor exists and the retries leave it alone, so it is read on the thread
    /// that called Start().
    const std::string& GetStartFailure() const { return m_startFailure; }

    /// True when that Start() failed because another socket holds the port.
    bool StartFoundPortInUse() const { return m_startFoundPortInUse; }

    /// Timestamp of the last received packet (microseconds since epoch).
    /// Compare across frames to detect new samples for interpolation.
    int64_t GetLastReceiveTimestamp() const { return m_lastReceiveTimestamp.load(std::memory_order_relaxed); }

    /// Gets the current rotation values with offset applied.
    /// @return True if data is available.
    bool GetRotation(float& yaw, float& pitch, float& roll) const;

    /// Gets the current position values with offset applied.
    /// @return True if position data is available.
    bool GetPosition(float& x, float& y, float& z) const;

    /// Sets the current rotation and position as the new center point.
    void Recenter();

    /// Clears the centering offset, so raw tracker values pass through untouched.
    /// PollingUdpReceiver and the C# OpenTrackReceiver have always had this; its
    /// absence here was a public-API asymmetry between two receivers of one protocol.
    void ResetOffset();

    /// Always false. The HCAM trailer is still parsed, but it no longer raises a
    /// recenter request: Headcam zeroes its own output on CENTER and the pipeline's
    /// centre is identity, so the zeroed stream is already correct. Kept on the API
    /// because mods call it; they simply never see a press.
    bool TryConsumeRecenterRequest() {
        return m_recenterRequested.exchange(false, std::memory_order_acq_rel);
    }

    /// Packets ignored because the tracker had stopped tracking and was
    /// repeating one pose. Non-zero means head tracking survived a dropout that
    /// would otherwise have swung the view to centre and back.
    uint64_t GetFrozenPacketCount() const {
        return m_frozenPackets.load(std::memory_order_relaxed);
    }

    /// Step to a different tracker app when the wrong one won the startup race.
    /// Safe to call from any thread; it takes effect on the next packet.
    void CycleSource() {
        m_cycleRequestedAtUs.store(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count(),
            std::memory_order_relaxed);
        m_cycleRequested.store(true, std::memory_order_release);
    }

    /// Packets dropped because they came from a second tracker source. Non-zero
    /// means two apps are sending to this port and one of them is being ignored.
    uint64_t GetRejectedPacketCount() const {
        return m_rejectedPackets.load(std::memory_order_relaxed);
    }

    /// Datagrams taken off the socket since Start(), whatever their length or content,
    /// one that was too long for the buffer included. With GetPublishedPoseCount() it
    /// tells a sender in the wrong format from no sender at all. A datagram is counted
    /// after it has been dealt with: whatever it changed is in place when the count moves.
    uint64_t GetDatagramCount() const { return m_datagrams.load(std::memory_order_acquire); }

    /// Of those, the ones published as the pose: parsed, from the source followed, and
    /// let through by the gate. GetLastReceiveTimestamp() moves with each, and the pose
    /// is in place when the count moves.
    uint64_t GetPublishedPoseCount() const { return m_publishedPoses.load(std::memory_order_acquire); }

    /// CENTER presses the followed tracker announced in the trailer. Nothing in core acts
    /// on one. A host whose output is relative (head movement turned into mouse movement)
    /// compares this across frames to drop the step the press makes in the pose.
    uint64_t GetAnnouncedCenterCount() const { return m_announcedCenters.load(std::memory_order_relaxed); }

private:
    friend struct detail::UdpReceiverTestAccess;

    void HandleDatagram(const char* buffer, int bytesReceived, const sockaddr_in& senderAddr, int64_t arrivedUs);
    void ReceiverThread();
    void SupervisorThread();
    bool BindAndReceive();
    void StopReceiverThread();

    // Thread-safe tracking data
    TrackingData m_trackingData;

    UdpSocket m_socket;
    std::thread m_thread;
    std::thread m_supervisorThread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopFlag{false};
#ifdef _WIN32
    /// Wakes the receive thread out of its wait for a datagram. Created with
    /// each receive thread and closed after it is joined.
    WSAEVENT m_stopEvent{WSA_INVALID_EVENT};
#endif
    std::atomic<bool> m_supervising{false};
    std::atomic<bool> m_retrying{false};
    std::atomic<bool> m_failed{false};
    /// Set by the receive thread when it gives up on a socket error, so the
    /// supervisor re-establishes the socket instead of leaving the receiver
    /// bound and permanently deaf.
    std::atomic<bool> m_receiveFailed{false};
    uint16_t m_port{kDefaultPort};
    std::function<void(const std::string&)> m_log = DefaultLogSink();

    // Offset for recentering
    std::atomic<float> m_yawOffset{0.0f};
    std::atomic<float> m_pitchOffset{0.0f};
    std::atomic<float> m_rollOffset{0.0f};
    std::atomic<float> m_posXOffset{0.0f};
    std::atomic<float> m_posYOffset{0.0f};
    std::atomic<float> m_posZOffset{0.0f};

    // Position data (mm, from OpenTrack)
    std::atomic<float> m_posX{0.0f};
    std::atomic<float> m_posY{0.0f};
    std::atomic<float> m_posZ{0.0f};
    std::atomic<bool> m_hasPosition{false};

    // Remote recenter (Headcam trailer). The trailer only rides packets sent
    // right after a CENTER press, so any sighting with a new counter is a
    // press. Counter state is receive-thread-only; Stop() resets it after
    // the join.
    std::atomic<bool> m_recenterRequested{false};
    uint8_t m_lastRecenterCounter{0};
    bool m_hasRecenterCounter{false};

    // Timestamp for connection detection
    std::atomic<int64_t> m_lastReceiveTimestamp{0};
    std::atomic<bool> m_isRemoteConnection{false};

    // The first sender seen, and anything else that turns up.
    //
    // Two tracker apps pointed at this port both get through, and the head pose
    // then alternates between them packet by packet - which looks exactly like
    // the view flicking between two positions, in every camera mode, and is
    // impossible to tell from a mod bug without knowing to look. Receive-thread
    // only; the counter is atomic so the outside can report it.
    uint64_t m_primarySource{0};
    // The source cycled away from, so the next lock does not land back on it.
    uint64_t m_avoidSource{0};
    std::atomic<bool> m_cycleRequested{false};
    std::atomic<int64_t> m_cycleRequestedAtUs{0};
    int64_t m_primaryLastSeenUs{0};
    std::atomic<uint64_t> m_rejectedPackets{0};
    std::atomic<uint64_t> m_datagrams{0};
    std::atomic<uint64_t> m_publishedPoses{0};
    std::atomic<uint64_t> m_announcedCenters{0};
    std::string m_startFailure;
    bool m_startFoundPortInUse{false};

    /// The first datagram that does not parse is logged, once per receive thread.
    bool m_parseFailLogged{false};

    /// Dropout rejection. Receive-thread only; Stop() resets it after the join.
    PoseJumpGate m_poseGate;
    std::atomic<uint64_t> m_frozenPackets{0};

    static constexpr int kMaxSeenSources = 8;
    uint64_t m_seenSources[kMaxSeenSources]{};
    int m_seenSourceCount{0};
};

}  // namespace cameraunlock
