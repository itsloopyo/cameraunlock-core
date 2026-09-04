#include "cameraunlock/protocol/udp_receiver.h"
#include "cameraunlock/logging/file_log.h"
#include "cameraunlock/protocol/opentrack_packet.h"
#include "cameraunlock/data/position_data.h"
#include <chrono>
#include <cmath>
#include <string>

namespace cameraunlock {

std::function<void(const std::string&)> DefaultLogSink() {
    return [](const std::string& message) {
        logging::Line("%s", message.c_str());
    };
}

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
        // The OS's own reason, not a guess at one. A bind fails for reasons
        // other than a port conflict, and a log line that names the wrong one
        // sends the user looking for an app that is not running.
        m_log("Failed to bind UDP port " + std::to_string(port) + ": " +
              m_socket.LastError() + " -- retrying every " +
              std::to_string(kRetryIntervalMs) + "ms until it succeeds");
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
                      " (" + std::to_string((now - retryingSinceUs) / 1000000) +
                      "s elapsed): " + m_socket.LastError());
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
                      " and it could not be reopened: " + m_socket.LastError() +
                      " -- retrying every " + std::to_string(kRetryIntervalMs) +
                      "ms until it succeeds");
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
    m_primarySource = 0;
    m_avoidSource = 0;
    m_cycleRequested.store(false, std::memory_order_relaxed);
    m_primaryLastSeenUs = 0;
    m_hasAcceptedPose = false;
    m_pendingValid = false;
    m_lastStepWasLarge = false;
    m_frozenPackets.store(0, std::memory_order_relaxed);
    m_seenSourceCount = 0;
    m_lastSourceYaw = 0.0f;
    m_lastSourcePitch = 0.0f;
    m_lastSourceRoll = 0.0f;
    m_rejectedPackets.store(0, std::memory_order_relaxed);
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

void UdpReceiver::ResetOffset() {
    m_yawOffset.store(0.0f, std::memory_order_relaxed);
    m_pitchOffset.store(0.0f, std::memory_order_relaxed);
    m_rollOffset.store(0.0f, std::memory_order_relaxed);
    m_posXOffset.store(0.0f, std::memory_order_relaxed);
    m_posYOffset.store(0.0f, std::memory_order_relaxed);
    m_posZOffset.store(0.0f, std::memory_order_relaxed);
}

namespace {

/// "ip:port" for a source packed as (s_addr << 16) | port.
std::string DescribeSource(uint64_t source) {
    in_addr addr = {};
    addr.s_addr = static_cast<uint32_t>(source >> 16);
    char ip[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &addr, ip, sizeof(ip));
    return std::string(ip) + ":" + std::to_string(static_cast<uint16_t>(source & 0xFFFF));
}

float LargestAxisChange(const TrackingPose& a, const TrackingPose& b) {
    const float dy = std::fabs(a.yaw - b.yaw);
    const float dp = std::fabs(a.pitch - b.pitch);
    const float dr = std::fabs(a.roll - b.roll);
    float worst = dy > dp ? dy : dp;
    return worst > dr ? worst : dr;
}

}  // namespace

// Takes `pose` as the truth the gate measures the next one against, discarding
// any jump it was still holding for confirmation.
void UdpReceiver::SeedPoseGate(const TrackingPose& pose) {
    m_hasAcceptedPose = true;
    m_pendingValid = false;
    m_lastStepWasLarge = false;
    m_acceptedPose = pose;
}

// Decides whether a freshly arrived pose is head tracking or the tracker having
// stopped tracking.
//
// A head tracker that loses the head does not say so - it just starts repeating
// one pose, usually centred. Followed faithfully, that swings the view from
// wherever the head was to centre and back every time tracking blinks, which is
// the single most visible failure this mod can have and is indistinguishable
// from a camera bug. Measured against a simulated eye-tracker dropout, each
// 200 ms blink moved the view 17 to 25 degrees.
//
// The tell is that the repeat is BIT-IDENTICAL. Real sensor output always
// jitters, so a value that arrives twice unchanged is not a measurement. That
// cannot be known until the repeat arrives, so a large jump is held back for one
// packet and only accepted once the following packet DIFFERS from it. Small
// changes - ordinary head movement - are never delayed.
bool UdpReceiver::AcceptPose(const TrackingPose& pose, bool repeatsPrevious) {
    if (!m_hasAcceptedPose) {
        SeedPoseGate(pose);
        return true;
    }

    if (repeatsPrevious) {
        // A resent duplicate is harmless - it is the value already published, so
        // publishing it again changes nothing. Refusing them would fight every
        // tracker app that resends faster than its sensor updates, which is most
        // of them, and that fight would itself look like jitter.
        //
        // The one case that matters is a repeat of a jump still awaiting
        // confirmation: a source that has stopped tracking repeats its "lost"
        // pose exactly, so the jump toward it was never a head movement. Keep
        // holding the last pose the head was actually in.
        if (m_pendingValid) {
            // The source has stopped moving, so whatever movement preceded this
            // is over: the next real step starts a new movement and gets the
            // confirmation hold again.
            m_lastStepWasLarge = false;
            m_frozenPackets.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        return true;
    }

    const float jump = LargestAxisChange(pose, m_acceptedPose);

    // Hold back the FIRST large step of a movement, never a continuing one.
    //
    // The `m_lastStepWasLarge` half is not a refinement, it is the whole
    // correctness of this gate. Without it, sustained fast movement is rejected
    // on every OTHER packet: one is held, the next is accepted, the one after
    // that is a large step again from the newly accepted pose, and so on. The
    // published pose then alternates between current and one packet stale for as
    // long as the head keeps moving, at half the tracker's rate - which is
    // exactly "the view flickers between two poses", and it appears only while
    // the head is moving, so a held test pose never shows it.
    //
    // Rejecting a packet also leaves `m_acceptedPose` behind, so the next step
    // measures even larger and the gate is more certain to trip again. It
    // self-sustains.
    //
    // The threshold reasons in degrees per PACKET, so what counts as "large"
    // depends on the tracker's sample rate: 300 deg/s is 5 degrees a packet at
    // 60 Hz but 9 at 33 Hz, and plenty of trackers (eye trackers especially) run
    // at the low end. A dropout is still caught, because the thing that
    // identifies one is not the size of the jump but that the pose STOPS moving
    // afterwards - which the repeat branch above tests directly.
    const bool sustainedMovement = m_lastStepWasLarge;
    if (jump > kConfirmJumpDegrees && !m_pendingValid && !sustainedMovement) {
        m_pendingValid = true;
        m_frozenPackets.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    m_pendingValid = false;
    m_lastStepWasLarge = jump > kConfirmJumpDegrees;
    m_acceptedPose = pose;
    return true;
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
            // Lock onto the first tracker that turns up and ignore any other.
            //
            // Nothing stops two apps sending to this port, and both used to get
            // through: the pose then alternates between them packet by packet
            // and the view flicks between two positions in every camera mode.
            // Averaging two trackers is never what anyone wants, and silently
            // doing it hides the real problem, so the second one is dropped and
            // named in the log.
            auto now = std::chrono::steady_clock::now();
            const int64_t arrivedUs = std::chrono::duration_cast<std::chrono::microseconds>(
                now.time_since_epoch()).count();

            const uint64_t source =
                (static_cast<uint64_t>(senderAddr.sin_addr.s_addr) << 16) | ntohs(senderAddr.sin_port);

            // Census of every endpoint that sends here, logged once each. Which
            // apps are really on this port - and whether one of them rotates its
            // source port - decides whether "ignore the second source" is even
            // the right rule, and neither is guessable from outside.
            if (m_log) {
                bool known = false;
                for (int i = 0; i < m_seenSourceCount; ++i) {
                    if (m_seenSources[i] == source) { known = true; break; }
                }
                if (!known && m_seenSourceCount < kMaxSeenSources) {
                    m_seenSources[m_seenSourceCount++] = source;
                    char ip[INET_ADDRSTRLEN] = {};
                    inet_ntop(AF_INET, &senderAddr.sin_addr, ip, sizeof(ip));
                    m_log(std::string("tracker source seen: ") + ip + ":" +
                          std::to_string(ntohs(senderAddr.sin_port)) + " (" +
                          std::to_string(m_seenSourceCount) + " distinct so far)");
                }
            }
            // Which app wins the lock is decided by whichever packet lands first
            // after the mod starts listening, and that is a race measured in
            // milliseconds - three senders arrived 6 ms apart on a real machine,
            // so "start the one you want first" does not decide it. When the
            // wrong one wins there is nothing the player can do about it from
            // inside the game, and the symptom (wrong sensitivity) does not look
            // like a source problem at all. This lets them step to the next one
            // until the view responds the way they expect.
            if (m_cycleRequested.exchange(false, std::memory_order_acq_rel)) {
                m_avoidSource = m_primarySource;
                m_primarySource = 0;
            }

            if (m_primarySource == 0) {
                // Skip the source just cycled away from, unless it is the only
                // one sending - in which case re-locking to it is the honest
                // outcome, and the log line says so.
                if (source == m_avoidSource &&
                    (arrivedUs - m_cycleRequestedAtUs.load(std::memory_order_relaxed)) / 1000 < kSourceHandoverMs) {
                    continue;
                }
                if (m_log && m_avoidSource != 0) {
                    char ip[INET_ADDRSTRLEN] = {};
                    inet_ntop(AF_INET, &senderAddr.sin_addr, ip, sizeof(ip));
                    m_log(std::string(source == m_avoidSource
                                          ? "only one tracker source is sending; staying on "
                                          : "switched to tracker source ") +
                          ip + ":" + std::to_string(ntohs(senderAddr.sin_port)));
                }
                m_avoidSource = 0;
                m_primarySource = source;
                m_primaryLastSeenUs = arrivedUs;
            } else if (source != m_primarySource) {
                // A challenger takes over ONLY if the incumbent has gone
                // SILENT. A still head is not a dead tracker.
                //
                // This used to hand over when the incumbent "stopped moving"
                // while a challenger was moving, so that an idle app winning the
                // race could not strand the player. With more than one app
                // actually sending - an OpenTrack instance and a vendor tool
                // like Tobii Game Hub, say - that rule flaps: hold your head
                // still for two seconds and the lock jumps to whichever other
                // app is jittering, move again and it jumps back. Two apps
                // rarely agree on scaling, so the view alternates between two
                // different amounts of head rotation, seconds apart, in every
                // camera mode. It survives any amount of work on the camera
                // because nothing about it is in the camera.
                //
                // Silence is the only signal that cannot be produced by the
                // player simply sitting still, so it is the only one used. An
                // idle app that wins the race is handled by SAYING SO - loudly,
                // and repeatedly - rather than by guessing.
                const bool incumbentSilent =
                    (arrivedUs - m_primaryLastSeenUs) / 1000 >= kSourceHandoverMs;

                if (incumbentSilent) {
                    if (m_log) {
                        char ip[INET_ADDRSTRLEN] = {};
                        inet_ntop(AF_INET, &senderAddr.sin_addr, ip, sizeof(ip));
                        m_log(std::string("tracker source went silent; switching to ") +
                              ip + ":" + std::to_string(ntohs(senderAddr.sin_port)));
                    }
                    m_primarySource = source;
                    m_primaryLastSeenUs = arrivedUs;
                } else {
                    if (m_rejectedPackets.fetch_add(1, std::memory_order_relaxed) == 0 && m_log) {
                        // Name BOTH endpoints. Which one is driving the view is
                        // the whole question when this fires, and the ignored
                        // source loses everything it sends - poses and CENTER
                        // presses alike - so a player whose recenter does
                        // nothing is reading the symptom of exactly this line.
                        m_log("a SECOND tracker source is sending to this port (" +
                              DescribeSource(source) + ") - IGNORING it; the view is driven by " +
                              DescribeSource(m_primarySource) +
                              ". Two sources make the head pose alternate between them, which"
                              " looks like the view flicking between two positions, and nothing"
                              " the ignored app sends reaches the game - including its recenter."
                              " Close whichever tracker you are not using.");
                    }
                    continue;
                }
            } else {
                m_primaryLastSeenUs = arrivedUs;
            }
            const int64_t nowUs = arrivedUs;

            // Re-arm first-sighting after a tracking gap: the tracker app
            // restarting resets its counter to zero, so a value latched from
            // the old session would swallow the first CENTER press of the new
            // one.
            int64_t prevTs = m_lastReceiveTimestamp.load(std::memory_order_relaxed);
            if (prevTs != 0 && (nowUs - prevTs) / 1000 >= kRecenterRearmMs) {
                m_hasRecenterCounter = false;
            }

            // Read the CENTER press BEFORE the pose gate below, never after.
            //
            // A press is announced metadata, not a measurement, and the packet
            // carrying one is precisely the shape the gate exists to reject: the
            // app has just subtracted its new neutral, so the pose lands a long
            // way from the last accepted one and then sits still. Parsed below
            // the gate's `continue`, a burst whose packets repeat bit-identically
            // is swallowed entire and the press never reaches the game - the
            // player recenters on their phone and the view does not move.
            uint8_t recenterCounter;
            const bool hasTrailer =
                OpenTrackPacket::TryParseRecenterCounter(buffer, bytesReceived, recenterCounter);
            // The trailer is parsed but no longer raises a recenter request.
            // Headcam owns centring: it zeroes its own output on CENTER, and the
            // pipeline's centre is identity by default, so the zeroed stream is
            // already correct without the mod doing anything.
            const bool pressed = hasTrailer &&
                (!m_hasRecenterCounter || recenterCounter != m_lastRecenterCounter);
            if (hasTrailer) {
                m_lastRecenterCounter = recenterCounter;
                m_hasRecenterCounter = true;
            }

            TrackingPose pose;
            PositionData position;
            const bool parsed = OpenTrackPacket::TryParseAll(buffer, bytesReceived, pose, position);
            if (!parsed && !s_parseFailLogged++ && m_log) {
                m_log("OpenTrack parse failed on " + std::to_string(bytesReceived) + "-byte packet");
            }
            if (parsed) {
                const bool repeatsPrevious = pose.yaw == m_lastSourceYaw &&
                                             pose.pitch == m_lastSourcePitch &&
                                             pose.roll == m_lastSourceRoll;
                if (!repeatsPrevious) {
                    m_lastSourceYaw = pose.yaw;
                    m_lastSourcePitch = pose.pitch;
                    m_lastSourceRoll = pose.roll;
                }

                // A press is the one discontinuity the tracker announces, so the
                // gate has nothing left to decide - take the pose it carries.
                // The trailer no longer drives a recenter, but it still marks the
                // jump to the app's new neutral as real, which is exactly what the
                // gate needs to know.
                //
                // A tracker that centres itself WITHOUT the trailer (opentrack's
                // Center bind) gets no such bypass, because from here that press
                // is indistinguishable from the head being lost - both are a
                // large jump followed by a pose that stops moving, which is the
                // whole reason this gate exists. It costs one held packet, and
                // if the tracker then repeats bit-identical values the hold
                // persists until the pose changes again. Telling the two apart
                // needs the announcement, which is what the trailer is for.
                if (pressed) {
                    SeedPoseGate(pose);
                } else if (!AcceptPose(pose, repeatsPrevious)) {
                    continue;
                }
                m_trackingData.Set(pose.yaw, pose.pitch, pose.roll);

                // Store position data
                m_posX.store(position.x, std::memory_order_relaxed);
                m_posY.store(position.y, std::memory_order_relaxed);
                m_posZ.store(position.z, std::memory_order_relaxed);
                m_hasPosition.store(true, std::memory_order_relaxed);

                m_isRemoteConnection.store(IsRemoteAddress(senderAddr), std::memory_order_relaxed);

                m_lastReceiveTimestamp.store(nowUs, std::memory_order_release);
            }
        }
    }
}

}  // namespace cameraunlock
