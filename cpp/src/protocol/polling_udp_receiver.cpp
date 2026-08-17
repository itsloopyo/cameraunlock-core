#include "cameraunlock/protocol/polling_udp_receiver.h"
#include "cameraunlock/protocol/opentrack_packet.h"
#include "cameraunlock/data/position_data.h"

#include <cmath>
#include <cstring>
#include <chrono>

namespace cameraunlock {

PollingUdpReceiver::~PollingUdpReceiver() {
    Shutdown();
}

bool PollingUdpReceiver::Initialize(uint16_t port) {
    if (m_initialized) {
        return true;
    }

    if (!m_socket.Open(port)) {
        return false;
    }

    m_initialized = true;
    m_lastReceiveTimeMs = 0;
    m_packetsReceived = 0;
    m_bytesReceived = 0;
    m_isRemoteConnection = false;
    m_hasData = false;
    m_hasOffset = false;
    m_lastRecenterCounter = 0;
    m_hasRecenterCounter = false;
    m_recenterRequested = false;
    std::memset(m_receiveBuffer, 0, sizeof(m_receiveBuffer));

    return true;
}

void PollingUdpReceiver::Shutdown() {
    if (!m_initialized) {
        return;
    }

    m_socket.Close();
    m_initialized = false;
}

bool PollingUdpReceiver::Poll() {
    if (!m_initialized || !m_socket.IsOpen()) {
        return false;
    }

    bool receivedAny = false;
    int packetsThisFrame = 0;
    constexpr int kMaxPacketsPerFrame = 1000;  // Safety limit

    // Re-arm first-sighting after a tracking gap: the tracker app restarting
    // resets its counter to zero, so a value latched from the old session
    // would swallow the first CENTER press of the new one.
    if (m_lastReceiveTimeMs != 0 &&
        GetCurrentTimeMs() - m_lastReceiveTimeMs >= kRecenterRearmMs) {
        m_hasRecenterCounter = false;
    }

    SOCKET sock = m_socket.GetHandle();

    // Drain ALL pending packets, keeping only the latest
    // This prevents lag from buffered packets when sender is faster than game fps
    while (packetsThisFrame < kMaxPacketsPerFrame) {
        sockaddr_in senderAddr;
#ifdef _WIN32
        int senderAddrLen = sizeof(senderAddr);
#else
        socklen_t senderAddrLen = sizeof(senderAddr);
#endif

        int bytesReceived = recvfrom(
            sock,
            m_receiveBuffer,
            static_cast<int>(sizeof(m_receiveBuffer)),
            0,
            reinterpret_cast<sockaddr*>(&senderAddr),
            &senderAddrLen
        );

        if (bytesReceived == SOCKET_ERROR) {
#ifdef _WIN32
            int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK) {
                break;  // No more data available
            }
            // The datagram was larger than m_receiveBuffer. It has already been consumed,
            // so this is not the end of the queue: breaking here let one oversized packet
            // from any LAN host discard every tracker packet still queued behind it.
            // Counted so the kMaxPacketsPerFrame bound still advances.
            if (error == WSAEMSGSIZE) {
                packetsThisFrame++;
                continue;
            }
#else
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                break;  // No more data available
            }
#endif
            break;  // Other error
        }

        if (bytesReceived == 0) {
            break;
        }

        packetsThisFrame++;

        if (ParsePacket(m_receiveBuffer, bytesReceived)) {
            m_packetsReceived++;
            m_bytesReceived += static_cast<uint64_t>(bytesReceived);
            receivedAny = true;

            m_isRemoteConnection = IsRemoteAddress(senderAddr);
        }
    }

    if (receivedAny) {
        m_lastReceiveTimeMs = GetCurrentTimeMs();
    }

    return receivedAny;
}

bool PollingUdpReceiver::GetPose(TrackingPose& pose) const {
    if (!m_hasData) {
        return false;
    }

    float yaw = m_yaw;
    float pitch = m_pitch;
    float roll = m_roll;

    if (m_hasOffset) {
        yaw -= m_yawOffset;
        pitch -= m_pitchOffset;
        roll -= m_rollOffset;
    }

    pose = TrackingPose(yaw, pitch, roll);
    return true;
}

bool PollingUdpReceiver::GetRawRotation(float& yaw, float& pitch, float& roll) const {
    if (!m_hasData) {
        return false;
    }

    yaw = m_yaw;
    pitch = m_pitch;
    roll = m_roll;
    return true;
}

bool PollingUdpReceiver::GetRotation(float& yaw, float& pitch, float& roll) const {
    if (!m_hasData) {
        return false;
    }

    yaw = m_yaw;
    pitch = m_pitch;
    roll = m_roll;

    if (m_hasOffset) {
        yaw -= m_yawOffset;
        pitch -= m_pitchOffset;
        roll -= m_rollOffset;
    }

    return true;
}

void PollingUdpReceiver::Recenter() {
    if (m_hasData) {
        m_yawOffset = m_yaw;
        m_pitchOffset = m_pitch;
        m_rollOffset = m_roll;
        m_hasOffset = true;
    }
    if (m_hasPosition) {
        m_posXOffset = m_posX;
        m_posYOffset = m_posY;
        m_posZOffset = m_posZ;
    }
}

void PollingUdpReceiver::ResetOffset() {
    m_yawOffset = 0.0f;
    m_pitchOffset = 0.0f;
    m_rollOffset = 0.0f;
    m_posXOffset = 0.0f;
    m_posYOffset = 0.0f;
    m_posZOffset = 0.0f;
    m_hasOffset = false;
}

bool PollingUdpReceiver::IsConnected() const {
    if (!m_initialized || m_lastReceiveTimeMs == 0) {
        return false;
    }

    int64_t elapsed = GetCurrentTimeMs() - m_lastReceiveTimeMs;
    return elapsed < kConnectionTimeoutMs;
}

bool PollingUdpReceiver::GetPosition(float& x, float& y, float& z) const {
    if (!m_hasPosition) {
        return false;
    }
    x = m_posX - m_posXOffset;
    y = m_posY - m_posYOffset;
    z = m_posZ - m_posZOffset;
    return true;
}

bool PollingUdpReceiver::ParsePacket(const char* buffer, int bytesReceived) {
    // Use shared OpenTrack packet parsing (rotation + position)
    TrackingPose pose;
    PositionData position;
    if (!OpenTrackPacket::TryParseAll(buffer, static_cast<size_t>(bytesReceived), pose, position)) {
        return false;
    }

    // Checked per datagram, not just on the one Poll() keeps: a whole recenter burst can
    // land inside a single game frame. Gated on the parse above, because the trailer only
    // means anything alongside the zeroed pose it rides with - honouring it on a packet
    // whose pose was rejected centres on the PREVIOUS pose, i.e. the pre-press drift the
    // tracker just cleared, which is the double-subtract failure by another route.
    uint8_t recenterCounter;
    if (OpenTrackPacket::TryParseRecenterCounter(buffer, static_cast<size_t>(bytesReceived), recenterCounter)) {
        if (!m_hasRecenterCounter || recenterCounter != m_lastRecenterCounter) {
            m_recenterRequested = true;
        }
        m_lastRecenterCounter = recenterCounter;
        m_hasRecenterCounter = true;
    }

    m_yaw = pose.yaw;
    m_pitch = pose.pitch;
    m_roll = pose.roll;
    m_hasData = true;

    m_posX = position.x;
    m_posY = position.y;
    m_posZ = position.z;
    m_hasPosition = true;

    return true;
}

int64_t PollingUdpReceiver::GetCurrentTimeMs() const {
    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
    return ms.count();
}

}  // namespace cameraunlock
