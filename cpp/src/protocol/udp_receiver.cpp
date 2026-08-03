#include "cameraunlock/protocol/udp_receiver.h"
#include "cameraunlock/protocol/opentrack_packet.h"
#include "cameraunlock/data/position_data.h"
#include <chrono>
#include <string>

namespace cameraunlock {

namespace {
constexpr int kRetrySleepIncrementMs = 100;
}

UdpReceiver::~UdpReceiver() {
    Stop();
}

bool UdpReceiver::Start(uint16_t port) {
    if (m_running.load(std::memory_order_acquire)) {
        return true;
    }
    if (m_retrying.load(std::memory_order_acquire)) {
        return false;
    }

    m_failed.store(false, std::memory_order_release);
    m_port = port;

    if (!m_socket.Open(port)) {
        m_failed.store(true, std::memory_order_release);
        if (m_log) {
            m_log("Failed to bind UDP port " + std::to_string(port) +
                  " -- will retry every " + std::to_string(kRetryIntervalMs / 1000) + "s");
        }
        StartRetryLoop();
        return false;
    }

    StartReceiverThread();
    return true;
}

void UdpReceiver::StartRetryLoop() {
    m_retrying.store(true, std::memory_order_release);
    m_retryThread = std::thread(&UdpReceiver::RetryThread, this);
}

void UdpReceiver::RetryThread() {
    const int sleepIncrements = kRetryIntervalMs / kRetrySleepIncrementMs;
    const int attemptsPerLog = kRetryLogIntervalMs / kRetryIntervalMs;
    int attempts = 0;

    while (m_retrying.load(std::memory_order_acquire)) {
        // Sleep in short increments so Stop() can interrupt quickly.
        for (int i = 0; i < sleepIncrements; ++i) {
            if (!m_retrying.load(std::memory_order_acquire)) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(kRetrySleepIncrementMs));
        }
        if (!m_retrying.load(std::memory_order_acquire)) return;

        attempts++;

        if (m_socket.Open(m_port)) {
            if (!m_retrying.load(std::memory_order_acquire)) {
                m_socket.Close();
                return;
            }

            m_failed.store(false, std::memory_order_release);
            m_retrying.store(false, std::memory_order_release);

            // Stop() blocks on this thread's join, so spinning up the receive
            // thread here is safe -- a concurrent Stop() will see the post-join
            // m_running and tear it down through the normal path.
            StartReceiverThread();

            if (m_log) {
                m_log("Bound UDP port " + std::to_string(m_port) +
                      " after " + std::to_string(attempts) + " retries");
            }
            return;
        }

        if (attempts % attemptsPerLog == 0 && m_log) {
            int elapsedSec = attempts * kRetryIntervalMs / 1000;
            m_log("Still waiting for UDP port " + std::to_string(m_port) +
                  " (" + std::to_string(elapsedSec) + "s elapsed)");
        }
    }
}

void UdpReceiver::StartReceiverThread() {
    m_stopFlag.store(false, std::memory_order_release);
    m_running.store(true, std::memory_order_release);
    m_thread = std::thread(&UdpReceiver::ReceiverThread, this);
}

void UdpReceiver::Stop() {
    m_retrying.store(false, std::memory_order_release);
    if (m_retryThread.joinable()) {
        m_retryThread.join();
    }

    if (m_running.load(std::memory_order_acquire)) {
        m_stopFlag.store(true, std::memory_order_release);
        if (m_thread.joinable()) {
            m_thread.join();
        }
        m_running.store(false, std::memory_order_release);
    }

    m_socket.Close();

    m_failed.store(false, std::memory_order_release);
    m_trackingData.Reset();
    m_yawOffset.store(0.0f, std::memory_order_relaxed);
    m_pitchOffset.store(0.0f, std::memory_order_relaxed);
    m_rollOffset.store(0.0f, std::memory_order_relaxed);
    m_posXOffset.store(0.0f, std::memory_order_relaxed);
    m_posYOffset.store(0.0f, std::memory_order_relaxed);
    m_posZOffset.store(0.0f, std::memory_order_relaxed);
    m_recenterRequested.store(false, std::memory_order_relaxed);
    m_lastRecenterCounter = 0;
    m_hasRecenterCounter = false;
    m_posX.store(0.0f, std::memory_order_relaxed);
    m_posY.store(0.0f, std::memory_order_relaxed);
    m_posZ.store(0.0f, std::memory_order_relaxed);
    m_hasPosition.store(false, std::memory_order_relaxed);
    m_lastReceiveTimestamp.store(0, std::memory_order_relaxed);
    m_isRemoteConnection.store(false, std::memory_order_relaxed);
}

bool UdpReceiver::IsReceiving() const {
    int64_t lastTs = m_lastReceiveTimestamp.load(std::memory_order_acquire);
    if (lastTs == 0) return false;

    auto now = std::chrono::steady_clock::now();
    int64_t nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();

    int64_t elapsedMs = (nowUs - lastTs) / 1000;
    return elapsedMs < kConnectionTimeoutMs;
}

bool UdpReceiver::GetRotation(float& yaw, float& pitch, float& roll) const {
    float rawYaw, rawPitch, rawRoll;
    if (!m_trackingData.Get(rawYaw, rawPitch, rawRoll)) {
        return false;
    }

    yaw = rawYaw - m_yawOffset.load(std::memory_order_relaxed);
    pitch = rawPitch - m_pitchOffset.load(std::memory_order_relaxed);
    roll = rawRoll - m_rollOffset.load(std::memory_order_relaxed);

    return true;
}

bool UdpReceiver::GetPosition(float& x, float& y, float& z) const {
    if (!m_hasPosition.load(std::memory_order_relaxed)) {
        return false;
    }
    x = m_posX.load(std::memory_order_relaxed) - m_posXOffset.load(std::memory_order_relaxed);
    y = m_posY.load(std::memory_order_relaxed) - m_posYOffset.load(std::memory_order_relaxed);
    z = m_posZ.load(std::memory_order_relaxed) - m_posZOffset.load(std::memory_order_relaxed);
    return true;
}

void UdpReceiver::Recenter() {
    float yaw, pitch, roll;
    if (m_trackingData.Get(yaw, pitch, roll)) {
        m_yawOffset.store(yaw, std::memory_order_relaxed);
        m_pitchOffset.store(pitch, std::memory_order_relaxed);
        m_rollOffset.store(roll, std::memory_order_relaxed);
    }
    if (m_hasPosition.load(std::memory_order_relaxed)) {
        m_posXOffset.store(m_posX.load(std::memory_order_relaxed), std::memory_order_relaxed);
        m_posYOffset.store(m_posY.load(std::memory_order_relaxed), std::memory_order_relaxed);
        m_posZOffset.store(m_posZ.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
}

void UdpReceiver::ReceiverThread() {
    constexpr size_t kReceiveBufferSize = 64;
    alignas(16) char buffer[kReceiveBufferSize];
    sockaddr_in senderAddr = {};
    int senderAddrSize = sizeof(senderAddr);

    SOCKET sock = m_socket.GetHandle();

#ifdef _WIN32
    WSAPOLLFD pollFd = {};
    pollFd.fd = sock;
    pollFd.events = POLLIN;
#endif

    int64_t s_recvErrLogged = 0;
    int64_t s_pollErrLogged = 0;
    int64_t s_firstPacketLogged = 0;
    int64_t s_shortPacketLogged = 0;
    int64_t s_parseFailLogged = 0;

    while (!m_stopFlag.load(std::memory_order_relaxed)) {
#ifdef _WIN32
        senderAddrSize = sizeof(senderAddr);
        int pollResult = WSAPoll(&pollFd, 1, 1);
        if (pollResult < 0) {
            if (!s_pollErrLogged++ && m_log) {
                m_log("WSAPoll failed with " + std::to_string(WSAGetLastError()) +
                      " - receiver thread exiting");
            }
            break;
        }
        if (pollResult == 0) continue;

        int bytesReceived = recvfrom(
            sock,
            buffer,
            sizeof(buffer),
            0,
            reinterpret_cast<sockaddr*>(&senderAddr),
            &senderAddrSize
        );
        if (bytesReceived == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK && !s_recvErrLogged++ && m_log) {
                m_log("recvfrom failed with WSA error " + std::to_string(err) +
                      " (continuing)");
            }
            continue;
        }
        if (!s_firstPacketLogged++ && m_log) {
            char ip[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &senderAddr.sin_addr, ip, sizeof(ip));
            m_log("First UDP packet received: " + std::to_string(bytesReceived) +
                  " bytes from " + ip + ":" + std::to_string(ntohs(senderAddr.sin_port)));
        }
        if (bytesReceived < static_cast<int>(OpenTrackPacket::kMinPacketSize)) {
            if (!s_shortPacketLogged++ && m_log) {
                m_log("packet too short: " + std::to_string(bytesReceived) +
                      " bytes (need >= " + std::to_string(OpenTrackPacket::kMinPacketSize) + ")");
            }
        }
#else
        socklen_t addrLen = sizeof(senderAddr);
        int bytesReceived = recvfrom(
            sock,
            buffer,
            sizeof(buffer),
            0,
            reinterpret_cast<sockaddr*>(&senderAddr),
            &addrLen
        );
#endif

        if (bytesReceived >= static_cast<int>(OpenTrackPacket::kMinPacketSize)) {
            auto now = std::chrono::steady_clock::now();
            int64_t nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
                now.time_since_epoch()).count();

            // Re-arm first-sighting after a tracking gap: the tracker app
            // restarting resets its counter to zero, so a value latched from
            // the old session would swallow the first CENTER press of the new
            // one.
            int64_t prevTs = m_lastReceiveTimestamp.load(std::memory_order_relaxed);
            if (prevTs != 0 && (nowUs - prevTs) / 1000 >= kConnectionTimeoutMs) {
                m_hasRecenterCounter = false;
            }

            TrackingPose pose;
            PositionData position;
            const bool parsed = OpenTrackPacket::TryParseAll(buffer, bytesReceived, pose, position);
            if (!parsed && !s_parseFailLogged++ && m_log) {
                m_log("OpenTrack parse failed on " + std::to_string(bytesReceived) + "-byte packet");
            }
            if (parsed) {
                m_trackingData.Set(pose.yaw, pose.pitch, pose.roll);

                // Store position data
                m_posX.store(position.x, std::memory_order_relaxed);
                m_posY.store(position.y, std::memory_order_relaxed);
                m_posZ.store(position.z, std::memory_order_relaxed);
                m_hasPosition.store(true, std::memory_order_relaxed);

                m_isRemoteConnection.store(IsRemoteAddress(senderAddr), std::memory_order_relaxed);

                m_lastReceiveTimestamp.store(nowUs, std::memory_order_release);
            }

            uint8_t recenterCounter;
            if (OpenTrackPacket::TryParseRecenterCounter(buffer, bytesReceived, recenterCounter)) {
                if (!m_hasRecenterCounter || recenterCounter != m_lastRecenterCounter) {
                    m_recenterRequested.store(true, std::memory_order_release);
                }
                m_lastRecenterCounter = recenterCounter;
                m_hasRecenterCounter = true;
            }
        }
    }
}

}  // namespace cameraunlock
