#include "cameraunlock/protocol/udp_receiver.h"
#include "cameraunlock/protocol/opentrack_packet.h"
#include "cameraunlock/data/position_data.h"
#include <chrono>
#include <string>

namespace cameraunlock {

namespace {

/// Supervisor wake-up granularity. Retries are timed off the clock rather than
/// off tick counts, so this only bounds how fast Stop() interrupts the thread.
constexpr int kSupervisorTickMs = 100;

int64_t NowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace

UdpReceiver::~UdpReceiver() {
    Stop();
}

bool UdpReceiver::Start(uint16_t port) {
    if (m_supervising.load(std::memory_order_acquire)) {
        return m_running.load(std::memory_order_acquire);
    }

    m_port = port;
    m_supervising.store(true, std::memory_order_release);

    const bool bound = BindAndReceive();
    if (!bound && m_log) {
        m_log("Failed to bind UDP port " + std::to_string(port) +
              " (another app is listening on it -- OpenTrack, or another"
              " game) -- retrying every " + std::to_string(kRetryIntervalMs) +
              "ms until it is free");
    }

    m_supervisorThread = std::thread(&UdpReceiver::SupervisorThread, this);
    return bound;
}

bool UdpReceiver::BindAndReceive() {
    if (!m_socket.Open(m_port)) {
        m_failed.store(true, std::memory_order_release);
        m_retrying.store(true, std::memory_order_release);
        return false;
    }

    m_failed.store(false, std::memory_order_release);
    m_retrying.store(false, std::memory_order_release);

    m_receiveFailed.store(false, std::memory_order_release);
    m_stopFlag.store(false, std::memory_order_release);
    m_running.store(true, std::memory_order_release);
    m_thread = std::thread(&UdpReceiver::ReceiverThread, this);
    return true;
}

void UdpReceiver::SupervisorThread() {
    int64_t retryingSinceUs = NowUs();
    int64_t lastAttemptUs = retryingSinceUs;
    int64_t lastWaitLogUs = retryingSinceUs;

    while (m_supervising.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kSupervisorTickMs));
        if (!m_supervising.load(std::memory_order_acquire)) return;

        const int64_t now = NowUs();

        if (m_retrying.load(std::memory_order_acquire)) {
            if (now - lastAttemptUs < static_cast<int64_t>(kRetryIntervalMs) * 1000) continue;
            lastAttemptUs = now;

            if (BindAndReceive()) {
                if (m_log) {
                    m_log("Bound UDP port " + std::to_string(m_port) + " after " +
                          std::to_string((now - retryingSinceUs) / 1000000) +
                          "s of waiting - tracking is live");
                }
            } else if (m_log && now - lastWaitLogUs >=
                                    static_cast<int64_t>(kRetryLogIntervalMs) * 1000) {
                lastWaitLogUs = now;
                m_log("Still waiting for UDP port " + std::to_string(m_port) +
                      " (" + std::to_string((now - retryingSinceUs) / 1000000) + "s elapsed)");
            }
            continue;
        }

        // Bound. Silence needs no action: a socket that another holder is
        // taking delivery from starts receiving again by itself the moment
        // that holder exits. A receive thread that died on a socket error is
        // the one state that never recovers on its own, so re-establish it.
        if (!m_receiveFailed.load(std::memory_order_acquire)) continue;

        StopReceiverThread();
        if (BindAndReceive()) {
            if (m_log) {
                m_log("Receive thread failed on UDP port " + std::to_string(m_port) +
                      " - socket re-established");
            }
        } else {
            retryingSinceUs = NowUs();
            lastAttemptUs = retryingSinceUs;
            lastWaitLogUs = retryingSinceUs;
            if (m_log) {
                m_log("Receive thread failed on UDP port " + std::to_string(m_port) +
                      " and another app has since taken it -- retrying every " +
                      std::to_string(kRetryIntervalMs) + "ms until it is free");
            }
        }
    }
}

void UdpReceiver::StopReceiverThread() {
    if (m_running.load(std::memory_order_acquire)) {
        m_stopFlag.store(true, std::memory_order_release);
        if (m_thread.joinable()) {
            m_thread.join();
        }
        m_running.store(false, std::memory_order_release);
    }
    m_socket.Close();
}

void UdpReceiver::Stop() {
    m_supervising.store(false, std::memory_order_release);
    if (m_supervisorThread.joinable()) {
        m_supervisorThread.join();
    }

    StopReceiverThread();

    m_retrying.store(false, std::memory_order_release);
    m_failed.store(false, std::memory_order_release);
    m_receiveFailed.store(false, std::memory_order_release);
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
            m_receiveFailed.store(true, std::memory_order_release);
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
