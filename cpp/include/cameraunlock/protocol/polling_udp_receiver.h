#pragma once

#include <cstdint>
#include "cameraunlock/data/tracking_pose.h"
#include "cameraunlock/protocol/socket_types.h"
#include "cameraunlock/protocol/udp_socket.h"

namespace cameraunlock {

/// Polling-based UDP receiver for OpenTrack protocol.
/// Designed for single-threaded game loops where Poll() is called each frame.
/// This is an alternative to the threaded UdpReceiver for engines that prefer
/// explicit polling over background threads.
class PollingUdpReceiver {
public:
    /// Default OpenTrack UDP port.
    static constexpr uint16_t kDefaultPort = 4242;

    /// Connection timeout in milliseconds.
    /// Higher than UdpReceiver (1000 vs 500) because the polling receiver must
    /// tolerate variable frame intervals in the game loop.
    static constexpr int kConnectionTimeoutMs = 1000;

    // See UdpReceiver::kRecenterRearmMs - the trailer re-arm window is a wire-contract
    // constant (~5s of silence), not the connection-liveness timeout.
    static constexpr int kRecenterRearmMs = 5000;

    /// Maximum receive buffer size.
    static constexpr size_t kMaxBufferSize = 256;

    PollingUdpReceiver() = default;
    ~PollingUdpReceiver();

    // Non-copyable, non-movable
    PollingUdpReceiver(const PollingUdpReceiver&) = delete;
    PollingUdpReceiver& operator=(const PollingUdpReceiver&) = delete;
    PollingUdpReceiver(PollingUdpReceiver&&) = delete;
    PollingUdpReceiver& operator=(PollingUdpReceiver&&) = delete;

    /// Initializes the UDP socket on the specified port.
    /// @param port The UDP port to listen on.
    /// @return True if initialization succeeded.
    bool Initialize(uint16_t port = kDefaultPort);

    /// Shuts down the receiver and releases resources.
    void Shutdown();

    /// Polls for incoming data (non-blocking).
    /// Drains all pending packets and keeps only the latest.
    /// Should be called once per frame from the main game loop.
    /// @return True if new data was received this frame.
    bool Poll();

    /// Gets the latest tracking pose (rotation only, with offset applied).
    /// @param pose Output pose if data is available.
    /// @return True if valid data is available.
    bool GetPose(TrackingPose& pose) const;

    /// Gets the raw rotation values without offset applied.
    /// @return True if valid data is available.
    bool GetRawRotation(float& yaw, float& pitch, float& roll) const;

    /// Gets the rotation values with offset applied.
    /// @return True if valid data is available.
    bool GetRotation(float& yaw, float& pitch, float& roll) const;

    /// Gets the latest position values with offset applied.
    /// @return True if position data is available.
    bool GetPosition(float& x, float& y, float& z) const;

    /// Sets the current rotation and position as the new center point.
    void Recenter();

    /// Resets the center offset to zero.
    void ResetOffset();

    /// Always false. The HCAM trailer is still parsed, but it no longer raises a
    /// recenter request: Headcam zeroes its own output on CENTER and the pipeline's
    /// centre is identity, so the zeroed stream is already correct. Kept on the API
    /// because mods call it; they simply never see a press.
    bool TryConsumeRecenterRequest() {
        bool requested = m_recenterRequested;
        m_recenterRequested = false;
        return requested;
    }

    /// True if the receiver is properly initialized.
    bool IsInitialized() const { return m_initialized; }

    /// True if data has been received recently (within timeout).
    bool IsConnected() const;

    /// True if the data source is from a remote (non-localhost) address.
    bool IsRemoteConnection() const { return m_isRemoteConnection; }

    /// Gets statistics about received data.
    uint64_t GetPacketsReceived() const { return m_packetsReceived; }
    uint64_t GetBytesReceived() const { return m_bytesReceived; }

private:
    bool ParsePacket(const char* buffer, int bytesReceived);
    int64_t GetCurrentTimeMs() const;

    UdpSocket m_socket;
    bool m_initialized = false;

    // Latest received data (rotation in degrees)
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;
    float m_roll = 0.0f;
    bool m_hasData = false;

    // Latest received position data (in mm)
    float m_posX = 0.0f;
    float m_posY = 0.0f;
    float m_posZ = 0.0f;
    bool m_hasPosition = false;

    // Center offset for recentering
    float m_yawOffset = 0.0f;
    float m_pitchOffset = 0.0f;
    float m_rollOffset = 0.0f;
    float m_posXOffset = 0.0f;
    float m_posYOffset = 0.0f;
    float m_posZOffset = 0.0f;
    bool m_hasOffset = false;

    // Remote recenter (Headcam trailer). The trailer only rides packets sent
    // right after a CENTER press, so any sighting with a new counter is a
    // press.
    uint8_t m_lastRecenterCounter = 0;
    bool m_hasRecenterCounter = false;
    bool m_recenterRequested = false;

    // Connection state
    int64_t m_lastReceiveTimeMs = 0;
    bool m_isRemoteConnection = false;

    // Statistics
    uint64_t m_packetsReceived = 0;
    uint64_t m_bytesReceived = 0;

    // Receive buffer
    char m_receiveBuffer[kMaxBufferSize];
};

}  // namespace cameraunlock
