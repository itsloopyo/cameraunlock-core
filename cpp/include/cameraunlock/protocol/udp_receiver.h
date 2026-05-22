#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <cstdint>
#include "cameraunlock/data/tracking_pose.h"
#include "cameraunlock/protocol/socket_types.h"
#include "cameraunlock/protocol/udp_socket.h"

namespace cameraunlock {

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

    /// Interval between bind retries when the port is held by another process.
    static constexpr int kRetryIntervalMs = 5000;

    /// Interval between "still waiting" retry log messages.
    static constexpr int kRetryLogIntervalMs = 30000;

    UdpReceiver() = default;
    ~UdpReceiver();

    // Non-copyable
    UdpReceiver(const UdpReceiver&) = delete;
    UdpReceiver& operator=(const UdpReceiver&) = delete;

    /// Starts the UDP receiver on the specified port.
    /// If the port is already in use, schedules a background retry every
    /// kRetryIntervalMs and returns false. The retry thread takes over without
    /// further action from the caller; once it binds successfully the receive
    /// thread starts and IsRunning becomes true.
    /// @return True if bound and the receive thread started immediately.
    bool Start(uint16_t port = kDefaultPort);

    /// Stops the UDP receiver. Cancels any pending retry, joins both threads,
    /// closes the socket, and clears tracking state.
    void Stop();

    /// Optional logging callback for bind failures and retry messages.
    /// Must be thread-safe: invoked from Start (caller thread) and from the
    /// background retry thread.
    void SetLog(std::function<void(const std::string&)> log) { m_log = std::move(log); }

    /// True if the receive thread is running.
    bool IsRunning() const { return m_running.load(std::memory_order_acquire); }

    /// True if a background retry is in progress (port currently unavailable).
    bool IsRetrying() const { return m_retrying.load(std::memory_order_acquire); }

    /// True if data has been received recently.
    bool IsReceiving() const;

    /// True if the data source is from a remote address.
    bool IsRemoteConnection() const { return m_isRemoteConnection.load(std::memory_order_relaxed); }

    /// True if the most recent bind attempt failed. Cleared once retry succeeds.
    bool IsFailed() const { return m_failed.load(std::memory_order_acquire); }

    /// Timestamp of the last received packet (microseconds since epoch).
    /// Compare across frames to detect new samples for interpolation.
    int64_t GetLastReceiveTimestamp() const { return m_lastReceiveTimestamp.load(std::memory_order_relaxed); }

    /// Gets the current rotation values with offset applied.
    /// @return True if data is available.
    bool GetRotation(float& yaw, float& pitch, float& roll) const;

    /// Gets the current position values (in mm, from OpenTrack).
    /// @return True if position data is available.
    bool GetPosition(float& x, float& y, float& z) const;

    /// Sets the current position as the new center point.
    void Recenter();

private:
    void ReceiverThread();
    void RetryThread();
    void StartRetryLoop();
    void StartReceiverThread();

    // Thread-safe tracking data
    TrackingData m_trackingData;

    UdpSocket m_socket;
    std::thread m_thread;
    std::thread m_retryThread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopFlag{false};
    std::atomic<bool> m_retrying{false};
    std::atomic<bool> m_failed{false};
    uint16_t m_port{kDefaultPort};
    std::function<void(const std::string&)> m_log;

    // Offset for recentering
    std::atomic<float> m_yawOffset{0.0f};
    std::atomic<float> m_pitchOffset{0.0f};
    std::atomic<float> m_rollOffset{0.0f};

    // Position data (mm, from OpenTrack)
    std::atomic<float> m_posX{0.0f};
    std::atomic<float> m_posY{0.0f};
    std::atomic<float> m_posZ{0.0f};
    std::atomic<bool> m_hasPosition{false};

    // Timestamp for connection detection
    std::atomic<int64_t> m_lastReceiveTimestamp{0};
    std::atomic<bool> m_isRemoteConnection{false};
};

}  // namespace cameraunlock
