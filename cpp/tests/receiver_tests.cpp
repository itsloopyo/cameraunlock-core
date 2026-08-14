// Receiver loopback tests: PollingUdpReceiver recenter trailer and offset
// behavior through the real socket drain path, plus UdpReceiver's supervisor
// reclaiming a port it could not get, or lost, to another process.

#include "cameraunlock/protocol/polling_udp_receiver.h"
#include "cameraunlock/protocol/opentrack_packet.h"
#include "cameraunlock/protocol/udp_receiver.h"
#include "cameraunlock/protocol/udp_socket.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>

namespace {

int g_failures = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

constexpr uint16_t kReceiverPort = 14261;
constexpr uint16_t kSenderPort = 14262;
constexpr uint16_t kSupervisedPort = 14263;
constexpr uint16_t kSupervisedSenderPort = 14264;
constexpr uint16_t kReusePort = 14265;

size_t BuildPacket(uint8_t out[54], double x, double y, double z,
                   double yaw, double pitch, double roll,
                   const uint8_t* recenterCounter = nullptr) {
    std::memcpy(out + 0,  &x,     sizeof(double));
    std::memcpy(out + 8,  &y,     sizeof(double));
    std::memcpy(out + 16, &z,     sizeof(double));
    std::memcpy(out + 24, &yaw,   sizeof(double));
    std::memcpy(out + 32, &pitch, sizeof(double));
    std::memcpy(out + 40, &roll,  sizeof(double));
    if (recenterCounter == nullptr) {
        return 48;
    }
    out[48] = 'H'; out[49] = 'C'; out[50] = 'A'; out[51] = 'M';
    out[52] = cameraunlock::OpenTrackPacket::kTrailerVersion;
    out[53] = *recenterCounter;
    return 54;
}

bool SendToPort(cameraunlock::UdpSocket& sender, uint16_t port,
                const uint8_t* data, size_t length) {
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    int sent = sendto(sender.GetHandle(), reinterpret_cast<const char*>(data),
                      static_cast<int>(length), 0,
                      reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    return sent == static_cast<int>(length);
}

bool SendTo(cameraunlock::UdpSocket& sender, const uint8_t* data, size_t length) {
    return SendToPort(sender, kReceiverPort, data, length);
}

/// Another process competing for the tracker port. Raw winsock because
/// UdpSocket deliberately omits SO_REUSEADDR, and the hijack case needs it.
SOCKET OpenCompetitor(uint16_t port, bool reuseAddr) {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return INVALID_SOCKET;
#ifdef _WIN32
    if (reuseAddr) {
        BOOL on = TRUE;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&on), sizeof(on));
    }
#else
    (void)reuseAddr;
#endif
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
#ifdef _WIN32
        closesocket(s);
#else
        close(s);
#endif
        return INVALID_SOCKET;
    }
    return s;
}

void CloseCompetitor(SOCKET s) {
    if (s == INVALID_SOCKET) return;
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

/// True once the receiver reports a packet newer than `sinceUs`, within
/// `timeoutMs`. Polls rather than sleeps so a fast path stays fast.
bool WaitForPacketAfter(cameraunlock::UdpReceiver& rx, int64_t sinceUs,
                        cameraunlock::UdpSocket& sender, uint16_t port,
                        const uint8_t* pkt, size_t len, int timeoutMs) {
    for (int elapsed = 0; elapsed < timeoutMs; elapsed += 50) {
        if (!SendToPort(sender, port, pkt, len)) return false;
        if (rx.GetLastReceiveTimestamp() > sinceUs) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return rx.GetLastReceiveTimestamp() > sinceUs;
}

bool WaitUntilRunning(cameraunlock::UdpReceiver& rx, int timeoutMs) {
    for (int elapsed = 0; elapsed < timeoutMs; elapsed += 25) {
        if (rx.IsRunning()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return rx.IsRunning();
}

bool PollUntilReceived(cameraunlock::PollingUdpReceiver& rx) {
    for (int i = 0; i < 500; i++) {
        if (rx.Poll()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

bool NearEqual(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

}  // namespace

int RunReceiverTests() {
    using cameraunlock::PollingUdpReceiver;
    using cameraunlock::UdpSocket;

    std::cout << "PollingUdpReceiver loopback tests:\n";
    g_failures = 0;

    PollingUdpReceiver rx;
    if (!rx.Initialize(kReceiverPort)) {
        Check(false, "receiver binds loopback test port");
        return g_failures;
    }
    UdpSocket sender;
    if (!sender.Open(kSenderPort)) {
        Check(false, "sender binds loopback test port");
        return g_failures;
    }

    uint8_t pkt[54];

    // Plain 48-byte packet: pose flows, no recenter request.
    size_t len = BuildPacket(pkt, 100.0, 50.0, -25.0, 10.0, 5.0, -2.0);
    Check(SendTo(sender, pkt, len) && PollUntilReceived(rx), "plain packet received");
    Check(!rx.TryConsumeRecenterRequest(), "plain packet requests no recenter");

    float x, y, z;
    Check(rx.GetPosition(x, y, z) && NearEqual(x, 1.0f) && NearEqual(y, 0.5f) && NearEqual(z, -0.25f),
          "position decoded without offset");

    // First trailer sighting is a press: the trailer only rides the
    // post-press burst.
    uint8_t counter = 7;
    len = BuildPacket(pkt, 100.0, 50.0, -25.0, 10.0, 5.0, -2.0, &counter);
    Check(SendTo(sender, pkt, len) && PollUntilReceived(rx), "trailered packet received");
    Check(rx.TryConsumeRecenterRequest(), "first trailer sighting requests recenter");
    Check(!rx.TryConsumeRecenterRequest(), "request is consumed exactly once");

    // Same counter again (rest of the burst): no new request.
    Check(SendTo(sender, pkt, len) && PollUntilReceived(rx), "repeat-counter packet received");
    Check(!rx.TryConsumeRecenterRequest(), "repeated counter does not retrigger");

    // Counter change: new request.
    counter = 8;
    len = BuildPacket(pkt, 100.0, 50.0, -25.0, 10.0, 5.0, -2.0, &counter);
    Check(SendTo(sender, pkt, len) && PollUntilReceived(rx), "new-counter packet received");
    Check(rx.TryConsumeRecenterRequest(), "counter change requests recenter");

    // Recenter captures rotation AND position; the next reads are deltas.
    rx.Recenter();
    float yaw, pitch, roll;
    Check(rx.GetRotation(yaw, pitch, roll) && NearEqual(yaw, 0.f) && NearEqual(pitch, 0.f),
          "rotation zeroed after recenter");
    Check(rx.GetPosition(x, y, z) && NearEqual(x, 0.f) && NearEqual(y, 0.f) && NearEqual(z, 0.f),
          "position zeroed after recenter");

    len = BuildPacket(pkt, 110.0, 50.0, -25.0, 10.0, 5.0, -2.0);
    Check(SendTo(sender, pkt, len) && PollUntilReceived(rx), "post-recenter packet received");
    Check(rx.GetPosition(x, y, z) && NearEqual(x, 0.1f) && NearEqual(y, 0.f) && NearEqual(z, 0.f),
          "position reads as delta from the recentered origin");

    rx.ResetOffset();
    Check(rx.GetPosition(x, y, z) && NearEqual(x, 1.1f) && NearEqual(y, 0.5f) && NearEqual(z, -0.25f),
          "ResetOffset restores raw position");

    rx.Shutdown();
    sender.Close();

    // ---- UdpReceiver supervisor: keeping hold of a contested tracker port ----
    std::cout << "UdpReceiver supervisor tests:\n";

    using cameraunlock::UdpReceiver;

    UdpSocket supervisedSender;
    if (!supervisedSender.Open(kSupervisedSenderPort)) {
        Check(false, "supervisor sender binds loopback test port");
        return g_failures;
    }
    len = BuildPacket(pkt, 0.0, 0.0, 0.0, 1.0, 2.0, 3.0);

    // The previous game is still holding the tracker port when this one starts.
    SOCKET occupier = OpenCompetitor(kSupervisedPort, false);
    Check(occupier != INVALID_SOCKET, "another process holds the tracker port");

    UdpReceiver supervised;
    Check(!supervised.Start(kSupervisedPort), "Start reports the port as unavailable");
    Check(supervised.IsRetrying(), "supervisor waits for the port");
    Check(!supervised.IsRunning(), "no receive thread while the port is held");

    // The user remembers, and closes the other game.
    CloseCompetitor(occupier);
    Check(WaitUntilRunning(supervised, 5 * UdpReceiver::kRetryIntervalMs),
          "binds within a few retry intervals of the port freeing up");
    Check(WaitForPacketAfter(supervised, 0, supervisedSender, kSupervisedPort, pkt, len, 2000),
          "tracker data flows once the port is reclaimed");

#ifdef _WIN32
    // Same scenario, except the game still running holds the port with
    // SO_REUSEADDR - which is how our own older mod builds held it. Windows
    // refuses a plain bind on top of that too, and it has to: a bind that
    // succeeded into a shared port would set the receiver running, report
    // tracking as live, and then sit deaf because the datagrams go to the
    // other holder. Failing is what keeps the port recoverable.
    SOCKET reuseOccupier = OpenCompetitor(kReusePort, true);
    Check(reuseOccupier != INVALID_SOCKET, "a SO_REUSEADDR process holds the tracker port");

    UdpReceiver againstReuse;
    Check(!againstReuse.Start(kReusePort),
          "Start refuses to share a port held with SO_REUSEADDR");
    Check(againstReuse.IsRetrying() && !againstReuse.IsRunning(),
          "supervisor waits rather than binding alongside and going deaf");

    CloseCompetitor(reuseOccupier);
    Check(WaitUntilRunning(againstReuse, 5 * UdpReceiver::kRetryIntervalMs),
          "binds once the SO_REUSEADDR holder exits");
    Check(WaitForPacketAfter(againstReuse, 0, supervisedSender, kReusePort, pkt, len, 2000),
          "tracker data flows after reclaiming from a SO_REUSEADDR holder");
    againstReuse.Stop();

    // The reason there is no "bound but silent, so re-bind" path: nothing can
    // take the stream away from a plain bind in the first place. Windows
    // refuses a SO_REUSEADDR bind on top of one (WSAEACCES).
    SOCKET hijacker = OpenCompetitor(kSupervisedPort, true);
    Check(hijacker == INVALID_SOCKET,
          "a bound port cannot be taken over by a SO_REUSEADDR process");
    CloseCompetitor(hijacker);
#endif

    supervised.Stop();
    Check(!supervised.IsRunning() && !supervised.IsRetrying(),
          "Stop tears the supervisor and receive thread down");
    supervisedSender.Close();

    if (g_failures == 0) {
        std::cout << "Receiver tests: all passed\n";
    } else {
        std::cout << "Receiver tests: " << g_failures << " failure(s)\n";
    }
    return g_failures;
}
