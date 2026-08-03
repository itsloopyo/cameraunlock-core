// PollingUdpReceiver loopback tests: recenter trailer and offset behavior
// through the real socket drain path.

#include "cameraunlock/protocol/polling_udp_receiver.h"
#include "cameraunlock/protocol/opentrack_packet.h"
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

bool SendTo(cameraunlock::UdpSocket& sender, const uint8_t* data, size_t length) {
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kReceiverPort);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    int sent = sendto(sender.GetHandle(), reinterpret_cast<const char*>(data),
                      static_cast<int>(length), 0,
                      reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    return sent == static_cast<int>(length);
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

    if (g_failures == 0) {
        std::cout << "Receiver tests: all passed\n";
    } else {
        std::cout << "Receiver tests: " << g_failures << " failure(s)\n";
    }
    return g_failures;
}
