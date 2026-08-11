// Tests for the C++ HeadTrackingSession per-frame pipeline.

#include <cameraunlock/tracking/head_tracking_session.h>

#include <cmath>
#include <iostream>

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

bool NearEqual(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) <= eps;
}

// Scriptable stand-in for UdpReceiver / PollingUdpReceiver.
struct FakeReceiver {
    bool hasRotation = false;
    float yaw = 0.f, pitch = 0.f, roll = 0.f;
    bool hasPosition = false;
    float posX = 0.f, posY = 0.f, posZ = 0.f;
    int64_t timestamp = 0;
    int recenterCalls = 0;
    // Emulate the real receivers' offset capture: after Recenter() the
    // reported rotation reads as zero.
    bool zeroOnRecenter = false;

    bool GetRotation(float& y, float& p, float& r) const {
        if (!hasRotation) return false;
        y = yaw; p = pitch; r = roll;
        return true;
    }
    bool GetPosition(float& x, float& y, float& z) const {
        if (!hasPosition) return false;
        x = posX; y = posY; z = posZ;
        return true;
    }
    int64_t GetLastReceiveTimestamp() const { return timestamp; }
    void Recenter() {
        recenterCalls++;
        if (zeroOnRecenter) {
            yaw = pitch = roll = 0.f;
        }
    }

    bool recenterRequested = false;
    bool TryConsumeRecenterRequest() {
        bool requested = recenterRequested;
        recenterRequested = false;
        return requested;
    }
};

// A receiver without TryConsumeRecenterRequest must keep compiling and
// updating -- the method is an optional part of the TReceiver contract.
struct MinimalReceiver {
    bool GetRotation(float& y, float& p, float& r) const {
        y = p = r = 0.f;
        return true;
    }
    bool GetPosition(float&, float&, float&) const { return false; }
    int64_t GetLastReceiveTimestamp() const { return 1; }
    void Recenter() {}
};

using Session = cameraunlock::HeadTrackingSession<FakeReceiver>;
using cameraunlock::TrackingMode;

void TestNoData() {
    std::cout << "No tracker data:\n";

    FakeReceiver rx;
    Session session(rx);

    Check(!session.Update(0.016f), "Update returns false with no rotation data");

    float y = 1.f, p = 1.f, r = 1.f;
    Check(!session.GetRotation(y, p, r) && y == 0.f && p == 0.f && r == 0.f,
          "rotation reports invalid and zeroed");

    float x = 1.f, py = 1.f, z = 1.f;
    Check(!session.GetPositionOffset(x, py, z) && x == 0.f && py == 0.f && z == 0.f,
          "position reports invalid and zeroed");
}

void TestRotationFlowsThrough() {
    std::cout << "Rotation pipeline:\n";

    FakeReceiver rx;
    rx.hasRotation = true;
    rx.yaw = 10.f; rx.pitch = 5.f; rx.roll = -2.f;
    rx.timestamp = 1;

    Session session(rx);
    session.SetStabilizationFrames(1000);  // keep auto-recenter out of this test

    // Smoothing needs several frames to converge on the target.
    bool updated = false;
    for (int i = 0; i < 600; i++) {
        rx.timestamp++;
        rx.yaw += (i % 2) ? 0.0001f : -0.0001f;  // keep samples "new"
        updated = session.Update(0.016f);
    }

    float y = 0.f, p = 0.f, r = 0.f;
    Check(updated && session.GetRotation(y, p, r), "Update succeeds and rotation is valid");
    Check(NearEqual(y, 10.f, 0.1f) && NearEqual(p, 5.f, 0.1f) && NearEqual(r, -2.f, 0.1f),
          "smoothed rotation converges to raw pose");
}

void TestStabilizationRecenter() {
    std::cout << "Stabilization auto-recenter:\n";

    FakeReceiver rx;
    rx.hasRotation = true;
    rx.timestamp = 1;

    Session session(rx);
    session.SetStabilizationFrames(5);

    for (int i = 0; i < 5; i++) {
        rx.timestamp++;
        session.Update(0.016f);
    }
    Check(rx.recenterCalls == 0, "no recenter before the settle window fills");

    rx.timestamp++;
    session.Update(0.016f);
    Check(rx.recenterCalls == 1, "recenter fires once the pose has been held");

    for (int i = 0; i < 10; i++) {
        rx.timestamp++;
        session.Update(0.016f);
    }
    Check(rx.recenterCalls == 1, "auto-recenter fires only once");
}

void TestAutoRecenterWaitsForSettle() {
    std::cout << "Auto-recenter settle window:\n";

    FakeReceiver rx;
    rx.hasRotation = true;
    rx.timestamp = 1;

    Session session(rx);
    session.SetStabilizationFrames(5);

    // A player still moving their head never fills the window.
    for (int i = 0; i < 200; i++) {
        rx.timestamp++;
        rx.yaw += 4.f;
        session.Update(0.016f);
    }
    Check(rx.recenterCalls == 0, "a moving pose never triggers the automatic recenter");

    // A frozen tracker reads as a perfectly still head; it must not count.
    for (int i = 0; i < 200; i++) {
        session.Update(0.016f);
    }
    Check(rx.recenterCalls == 0, "a tracker that stopped sending does not count as settled");

    // Held still, within jitter, for the length of the window.
    for (int i = 0; i < 6; i++) {
        rx.timestamp++;
        rx.yaw += (i % 2) ? 0.2f : -0.2f;
        session.Update(0.016f);
    }
    Check(rx.recenterCalls == 1, "a held pose captures the center");
    Check(session.HasCentered(), "session reports it has centered");
}

void TestModeCycling() {
    std::cout << "Mode cycling:\n";

    FakeReceiver rx;
    rx.hasRotation = true;
    rx.yaw = 20.f;
    rx.hasPosition = true;
    rx.posX = 0.1f;
    rx.timestamp = 1;

    Session session(rx);
    session.SetStabilizationFrames(1000);

    Check(session.GetMode() == TrackingMode::RotationAndPosition, "starts in 6DOF mode");
    Check(session.IsRotationActive() && session.IsPositionActive(), "both axes active in 6DOF");

    Check(session.CycleMode() == TrackingMode::RotationOnly, "first cycle lands on rotation-only");
    Check(session.IsRotationActive() && !session.IsPositionActive(), "position inactive");

    for (int i = 0; i < 60; i++) { rx.timestamp++; session.Update(0.016f); }
    float x = 9.f, y = 9.f, z = 9.f;
    Check(!session.GetPositionOffset(x, y, z) && x == 0.f, "position zeroed in rotation-only mode");

    Check(session.CycleMode() == TrackingMode::PositionOnly, "second cycle lands on position-only");
    for (int i = 0; i < 60; i++) { rx.timestamp++; session.Update(0.016f); }
    float ry = 9.f, rp = 9.f, rr = 9.f;
    session.GetRotation(ry, rp, rr);
    Check(ry == 0.f && rp == 0.f && rr == 0.f, "rotation zeroed in position-only mode");

    Check(session.CycleMode() == TrackingMode::RotationAndPosition, "third cycle wraps to 6DOF");
}

void TestDuplicatePacketFiltering() {
    std::cout << "Duplicate-packet filtering:\n";

    FakeReceiver rx;
    rx.hasRotation = true;
    rx.yaw = 1.f;
    rx.timestamp = 1;

    Session session(rx);
    session.SetStabilizationFrames(1000);

    session.Update(0.016f);
    Check(session.WasNewSample(), "first packet counts as a new sample");

    // Same timestamp -> not even a new packet.
    session.Update(0.016f);
    Check(!session.WasNewSample(), "same timestamp is not a new sample");

    // New packet, identical data -> filtered out.
    rx.timestamp = 2;
    session.Update(0.016f);
    Check(!session.WasNewSample(), "new packet with identical data is not a new sample");

    // New packet, changed data -> new sample.
    rx.timestamp = 3;
    rx.yaw = 2.f;
    session.Update(0.016f);
    Check(session.WasNewSample(), "new packet with changed data is a new sample");
}

void TestRecenterZeroesPose() {
    std::cout << "Recenter:\n";

    FakeReceiver rx;
    rx.hasRotation = true;
    rx.yaw = 30.f; rx.pitch = -10.f;
    rx.hasPosition = true;
    rx.posX = 0.2f; rx.posY = 0.1f; rx.posZ = -0.05f;
    rx.timestamp = 1;

    Session session(rx);
    session.SetStabilizationFrames(1000);

    for (int i = 0; i < 300; i++) { rx.timestamp++; session.Update(0.016f); }

    session.Recenter();
    Check(rx.recenterCalls == 1, "receiver-level recenter invoked");

    // The fake receiver does not subtract its own offset, so emulate it.
    rx.yaw = 0.f; rx.pitch = 0.f; rx.roll = 0.f;

    for (int i = 0; i < 300; i++) { rx.timestamp++; session.Update(0.016f); }

    float y = 9.f, p = 9.f, r = 9.f;
    session.GetRotation(y, p, r);
    Check(NearEqual(y, 0.f, 0.05f) && NearEqual(p, 0.f, 0.05f), "pose settles at zero after recenter");

    float x = 9.f, py = 9.f, z = 9.f;
    session.GetPositionOffset(x, py, z);
    Check(NearEqual(x, 0.f, 0.005f) && NearEqual(py, 0.f, 0.005f) && NearEqual(z, 0.f, 0.005f),
          "position offset settles at zero after recenter (center captured)");
}

void TestRemoteRecenterRequest() {
    std::cout << "Remote recenter request (packet trailer):\n";

    FakeReceiver rx;
    rx.hasRotation = true;
    rx.yaw = 30.f;
    rx.timestamp = 1;

    Session session(rx);
    session.SetStabilizationFrames(5);

    session.Update(0.016f);
    Check(rx.recenterCalls == 0, "no recenter without a request");

    rx.recenterRequested = true;
    session.Update(0.016f);
    Check(rx.recenterCalls == 1, "request triggers a recenter");
    Check(!rx.recenterRequested, "request is consumed");

    for (int i = 0; i < 10; i++) {
        rx.timestamp++;
        session.Update(0.016f);
    }
    Check(rx.recenterCalls == 1,
          "remote recenter counts as centered - stabilization auto-recenter does not re-fire");

    MinimalReceiver minimalRx;
    cameraunlock::HeadTrackingSession<MinimalReceiver> minimalSession(minimalRx);
    Check(minimalSession.Update(0.016f), "receiver without the request method still works");

    static_assert(Session::kHasRemoteRecenter,
                  "FakeReceiver forwards TryConsumeRecenterRequest");
    static_assert(!cameraunlock::HeadTrackingSession<MinimalReceiver>::kHasRemoteRecenter,
                  "MinimalReceiver has no remote recenter");
}

void TestRecenterSeedsCurrentFrameWithCenteredPose() {
    std::cout << "Recenter frame seeding:\n";

    FakeReceiver rx;
    rx.hasRotation = true;
    rx.yaw = 30.f;
    rx.timestamp = 1;
    rx.zeroOnRecenter = true;

    Session session(rx);
    session.SetStabilizationFrames(1000);

    session.Update(0.016f);

    rx.recenterRequested = true;
    rx.timestamp = 2;
    session.Update(0.016f);
    Check(NearEqual(session.GetLastRaw().yaw, 0.f),
          "consume frame is seeded with the centered pose, not the stale pre-recenter fetch");
}

}  // namespace

int RunSessionTests() {
    std::cout << "\nHeadTrackingSession tests:\n";
    g_failures = 0;

    TestNoData();
    TestRotationFlowsThrough();
    TestStabilizationRecenter();
    TestAutoRecenterWaitsForSettle();
    TestModeCycling();
    TestDuplicatePacketFiltering();
    TestRecenterZeroesPose();
    TestRemoteRecenterRequest();
    TestRecenterSeedsCurrentFrameWithCenteredPose();

    if (g_failures == 0) {
        std::cout << "Session tests: all passed\n";
    } else {
        std::cout << "Session tests: " << g_failures << " failure(s)\n";
    }
    return g_failures;
}
