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
    // Emulate the real receivers' offset capture: Recenter() stores the current
    // pose and GetRotation reports the difference, so the raw pose keeps moving
    // underneath a captured center the way a real tracker's does.
    bool zeroOnRecenter = false;
    float offYaw = 0.f, offPitch = 0.f, offRoll = 0.f;

    bool GetRotation(float& y, float& p, float& r) const {
        if (!hasRotation) return false;
        y = yaw - offYaw; p = pitch - offPitch; r = roll - offRoll;
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
            offYaw = yaw; offPitch = pitch; offRoll = roll;
        }
    }

    bool recenterRequested = false;
    bool TryConsumeRecenterRequest() {
        bool requested = recenterRequested;
        recenterRequested = false;
        return requested;
    }
};

// A receiver without TryConsumeRecenterRequest OR IsRemoteConnection must keep
// compiling and updating -- both are optional parts of the TReceiver contract.
// Kept deliberately as the graceful-degradation case; TestGracefulDegradation
// asserts what that degradation actually looks like rather than leaving it
// implied.
struct MinimalReceiver {
    bool GetRotation(float& y, float& p, float& r) const {
        y = p = r = 0.f;
        return true;
    }
    bool GetPosition(float&, float&, float&) const { return false; }
    int64_t GetLastReceiveTimestamp() const { return 1; }
    void Recenter() {}
};

// The receiver shape the real UdpReceiver / PollingUdpReceiver present: a
// scriptable IsRemoteConnection() so the session can select between the two
// smoothing parameters itself. Neither FakeReceiver nor MinimalReceiver had one,
// so kHasRemoteConnection was false for every session the suite instantiated and
// the whole propagation block was discarded by if constexpr in all 188
// assertions.
struct RemoteAwareReceiver {
    bool hasRotation = true;
    float yaw = 0.f, pitch = 0.f, roll = 0.f;
    bool hasPosition = false;
    float posX = 0.f, posY = 0.f, posZ = 0.f;
    int64_t timestamp = 1;
    bool isRemote = false;

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
    void Recenter() {}
    bool IsRemoteConnection() const { return isRemote; }
};

// The same shape with IsRemoteConnection() lacking the const qualifier. An
// adapter written this way is easy to produce by hand and used to fail detection
// with ZERO diagnostic: the trait probed through declval<const T&>() while its
// sibling HasRecenterRequest probed through declval<T&>(), so the mods'
// static_assert(kHasRemoteRecenter) pattern passed while remote connections
// silently reported local forever and every remote user got 0.0 instead of 0.15.
struct NonConstRemoteReceiver {
    float yaw = 0.f;
    int64_t timestamp = 1;
    bool isRemote = true;

    bool GetRotation(float& y, float& p, float& r) const {
        y = yaw; p = 0.f; r = 0.f;
        return true;
    }
    bool GetPosition(float&, float&, float&) const { return false; }
    int64_t GetLastReceiveTimestamp() const { return timestamp; }
    void Recenter() {}
    bool IsRemoteConnection() { return isRemote; }
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
    session.SetAutoRecenterOnConnect(true);
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
    session.SetAutoRecenterOnConnect(true);
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

void TestAutoRecenterOnConnectIsOffByDefault() {
    std::cout << "Auto-recenter on connect defaults off:\n";

    FakeReceiver rx;
    rx.hasRotation = true;
    rx.yaw = 40.f;
    rx.timestamp = 1;

    Session session(rx);
    session.SetStabilizationFrames(5);

    // A pose held perfectly still for far longer than the settle window is
    // exactly what used to arm the capture. Every tracker in use centers itself,
    // so a session-start capture only adds a second center in series with the
    // tracker's - and a Center press in the tracker app then parks the view at
    // the negated drift until the player also hits the mod's hotkey.
    for (int i = 0; i < 200; i++) {
        rx.timestamp++;
        session.Update(0.016f);
    }

    Check(rx.recenterCalls == 0, "no center is captured on connect by default");
    Check(!session.HasCentered(), "session reports it has not centered");

    float y = 9.f, p = 9.f, r = 9.f;
    Check(session.GetRotation(y, p, r) && NearEqual(y, 40.f, 0.5f),
          "the incoming pose passes through uncentered");
}

void TestTrackerSideCenterLandsAtZero() {
    std::cout << "Tracker-side center without a trailer:\n";

    // The opentrack case. opentrack has its own Center bind and sends no HCAM
    // trailer, so all the session sees is the stream dropping to zero. A
    // session-start capture would still be subtracting the pre-press pose,
    // parking the view at the negated drift until the player also hits the
    // mod's recenter hotkey.
    FakeReceiver rx;
    rx.hasRotation = true;
    // The C++ port centres at the RECEIVER, so the fake has to apply an offset or a
    // wrongly-fired capture would not move the output and this test could not see it.
    rx.zeroOnRecenter = true;
    rx.yaw = 40.f; rx.pitch = 20.f; rx.roll = 10.f;
    rx.timestamp = 1;

    Session session(rx);

    for (int i = 0; i < 200; i++) { rx.timestamp++; session.Update(0.016f); }

    float uy = 9.f, up = 9.f, ur = 9.f;
    session.GetRotation(uy, up, ur);
    Check(NearEqual(uy, 40.f, 0.5f),
          "the drifted pose reaches the output uncentred, so the check below is not trivial");

    // The tracker subtracts its own neutral and the stream goes to zero.
    rx.yaw = 0.f; rx.pitch = 0.f; rx.roll = 0.f;
    for (int i = 0; i < 300; i++) { rx.timestamp++; session.Update(0.016f); }

    float y = 9.f, p = 9.f, r = 9.f;
    session.GetRotation(y, p, r);
    Check(NearEqual(y, 0.f, 0.05f) && NearEqual(p, 0.f, 0.05f) && NearEqual(r, 0.f, 0.05f),
          "view lands at zero on a tracker-side center, not the negated drift");

    // And the centre really is identity, not merely cancelling this pose.
    rx.yaw = 20.f;
    for (int i = 0; i < 300; i++) { rx.timestamp++; session.Update(0.016f); }
    session.GetRotation(y, p, r);
    Check(NearEqual(y, 20.f, 0.1f), "20 degrees off the tracker's neutral reads as 20");
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

void TestManualRecenterDisarmsTheAutomaticOne() {
    std::cout << "Manual recenter disarms the automatic one:" << std::endl;

    FakeReceiver rx;
    rx.hasRotation = true;
    rx.zeroOnRecenter = true;
    rx.yaw = 0.f; rx.pitch = 0.f;
    rx.timestamp = 1;

    Session session(rx);
    session.SetAutoRecenterOnConnect(true);
    session.SetStabilizationFrames(10);

    // Recenter deliberately, before the automatic one has had its chance.
    session.Recenter();
    Check(rx.recenterCalls == 1, "the deliberate recenter reached the receiver");

    // Now hold a pose perfectly still for far longer than the stabilization
    // window. That is exactly what arms the automatic recenter, and it must not
    // fire: it would capture THIS pose as centre and throw away the one the
    // player chose.
    rx.yaw = 25.f;
    for (int i = 0; i < 200; i++) { rx.timestamp++; session.Update(0.016f); }

    Check(rx.recenterCalls == 1,
          "holding still after a manual recenter does not recenter again");

    float y = 9.f, p = 9.f, r = 9.f;
    session.GetRotation(y, p, r);
    Check(NearEqual(y, 25.f, 0.1f),
          "the pose held after the manual recenter still reads 25, not 0");
}

void TestRecenterZeroesPose() {
    std::cout << "Recenter:\n";

    FakeReceiver rx;
    rx.hasRotation = true;
    rx.yaw = 30.f; rx.pitch = -10.f;
    rx.hasPosition = true;
    rx.posX = 0.2f; rx.posY = 0.1f; rx.posZ = -0.05f;
    rx.timestamp = 1;

    rx.zeroOnRecenter = true;

    Session session(rx);

    for (int i = 0; i < 300; i++) { rx.timestamp++; session.Update(0.016f); }

    session.Recenter();
    Check(rx.recenterCalls == 1, "receiver-level recenter invoked");

    for (int i = 0; i < 300; i++) { rx.timestamp++; session.Update(0.016f); }

    float y = 9.f, p = 9.f, r = 9.f;
    session.GetRotation(y, p, r);
    Check(NearEqual(y, 0.f, 0.05f) && NearEqual(p, 0.f, 0.05f), "pose settles at zero after recenter");

    float x = 9.f, py = 9.f, z = 9.f;
    session.GetPositionOffset(x, py, z);
    Check(NearEqual(x, 0.f, 0.005f) && NearEqual(py, 0.f, 0.005f) && NearEqual(z, 0.f, 0.005f),
          "position offset settles at zero after recenter (center captured)");

    // Pin the captured centre: 20 degrees past a centre of 30 reads as 20.
    rx.yaw = 50.f;
    for (int i = 0; i < 300; i++) { rx.timestamp++; session.Update(0.016f); }
    session.GetRotation(y, p, r);
    Check(NearEqual(y, 20.f, 0.1f), "rotation is measured from the captured centre");
}

void TestRemoteRecenterRequest() {
    std::cout << "Remote recenter request (packet trailer):\n";

    FakeReceiver rx;
    rx.hasRotation = true;
    rx.yaw = 30.f;
    rx.timestamp = 1;

    Session session(rx);
    session.SetAutoRecenterOnConnect(true);
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

    session.Update(0.016f);

    rx.recenterRequested = true;
    rx.timestamp = 2;
    session.Update(0.016f);
    Check(NearEqual(session.GetLastRaw().yaw, 0.f),
          "consume frame is seeded with the centered pose, not the stale pre-recenter fetch");
}

using RemoteSession = cameraunlock::HeadTrackingSession<RemoteAwareReceiver>;

void TestConnectionFlagReachesBothProcessors() {
    std::cout << "Connection flag propagation:\n";

    static_assert(RemoteSession::kHasRemoteConnection,
                  "RemoteAwareReceiver exposes IsRemoteConnection");

    RemoteAwareReceiver rx;
    rx.isRemote = true;
    RemoteSession session(rx);

    // Pre-poison both processors so a session that simply leaves them alone fails.
    session.GetProcessor().SetIsRemoteConnection(false);
    session.GetPositionProcessor().SetIsRemoteConnection(false);

    session.Update(0.016f);

    Check(session.IsRemoteConnection(), "session reports the remote connection");
    Check(session.GetProcessor().IsRemoteConnection(),
          "rotation processor received the remote flag");
    Check(session.GetPositionProcessor().IsRemoteConnection(),
          "position processor received the remote flag");

    RemoteAwareReceiver localRx;
    localRx.isRemote = false;
    RemoteSession localSession(localRx);
    localSession.GetProcessor().SetIsRemoteConnection(true);
    localSession.GetPositionProcessor().SetIsRemoteConnection(true);

    localSession.Update(0.016f);

    Check(!localSession.IsRemoteConnection(), "session reports the local connection");
    Check(!localSession.GetProcessor().IsRemoteConnection(),
          "rotation processor received the local flag");
    Check(!localSession.GetPositionProcessor().IsRemoteConnection(),
          "position processor received the local flag");
}

void TestLiveConnectionSwitch() {
    std::cout << "Live local -> remote -> local switch:\n";

    RemoteAwareReceiver rx;
    rx.isRemote = false;
    RemoteSession session(rx);

    session.Update(0.016f);
    Check(!session.IsRemoteConnection(), "starts local");

    // A user unplugging a local OpenTrack instance and picking up a phone on WiFi
    // must get the other parameter without restarting the game.
    rx.isRemote = true;
    rx.timestamp++;
    session.Update(0.016f);
    Check(session.IsRemoteConnection(), "switch to remote is picked up mid-session");
    Check(session.GetProcessor().IsRemoteConnection(), "rotation processor follows the switch");
    Check(session.GetPositionProcessor().IsRemoteConnection(),
          "position processor follows the switch");

    rx.isRemote = false;
    rx.timestamp++;
    session.Update(0.016f);
    Check(!session.IsRemoteConnection(), "switch back to local is picked up too");
    Check(!session.GetProcessor().IsRemoteConnection(), "rotation processor follows back");
    Check(!session.GetPositionProcessor().IsRemoteConnection(), "position processor follows back");
}

void TestSmoothingSelectionFollowsTheConnection() {
    std::cout << "Smoothing selection follows the connection:\n";

    // The flag reaching the processor is only half the contract. This asserts the
    // pose actually moves differently, which is what the user feels.
    auto stepMagnitude = [](bool isRemote) {
        RemoteAwareReceiver rx;
        rx.isRemote = isRemote;
        RemoteSession session(rx);
        session.SetLocalSmoothing(0.0f);
        session.SetRemoteSmoothing(0.95f);
        session.SetMaxExtrapolationFraction(0.0f);

        rx.yaw = 0.f;
        for (int i = 0; i < 5; i++) {
            rx.timestamp++;
            session.Update(0.016f);
        }

        rx.yaw = 20.f;
        rx.timestamp++;
        session.Update(0.016f);

        float y = 0.f, p = 0.f, r = 0.f;
        session.GetRotation(y, p, r);
        return y;
    };

    float local = stepMagnitude(false);
    float remote = stepMagnitude(true);

    Check(local > remote,
          "a remote connection smooths harder than a local one");
    Check(remote > 0.f, "a remote connection still tracks the target");
    Check(local < 20.f, "frame interpolation still applies at local smoothing 0");
}

void TestNonConstIsRemoteConnectionIsDetected() {
    std::cout << "Non-const IsRemoteConnection detection:\n";

    using NonConstSession = cameraunlock::HeadTrackingSession<NonConstRemoteReceiver>;

    // The regression this guards: detection through declval<const T&>() rejected a
    // non-const IsRemoteConnection() silently, and the session then reported local
    // forever. Nothing in the mods' static_assert pattern caught it because the
    // sibling recenter trait was looser and still passed.
    static_assert(NonConstSession::kHasRemoteConnection,
                  "an adapter whose IsRemoteConnection() is not const must still be detected");

    NonConstRemoteReceiver rx;
    rx.isRemote = true;
    NonConstSession session(rx);

    session.Update(0.016f);

    Check(session.IsRemoteConnection(),
          "a non-const IsRemoteConnection() reaches the session");
    Check(session.GetProcessor().IsRemoteConnection(),
          "a non-const IsRemoteConnection() reaches the rotation processor");
    Check(session.GetPositionProcessor().IsRemoteConnection(),
          "a non-const IsRemoteConnection() reaches the position processor");
}

void TestGracefulDegradation() {
    std::cout << "Graceful degradation without IsRemoteConnection:\n";

    using MinimalSession = cameraunlock::HeadTrackingSession<MinimalReceiver>;

    static_assert(!MinimalSession::kHasRemoteConnection,
                  "MinimalReceiver has no IsRemoteConnection");
    static_assert(!Session::kHasRemoteConnection,
                  "FakeReceiver has no IsRemoteConnection either");

    MinimalReceiver rx;
    MinimalSession session(rx);

    Check(session.Update(0.016f), "a receiver without the method still updates");
    Check(!session.IsRemoteConnection(),
          "the session reports local, which is the documented degradation");

    // The mod is expected to drive the flag itself in this case, and the session
    // must not fight it: with no detection there is no per-frame overwrite.
    session.GetProcessor().SetIsRemoteConnection(true);
    session.GetPositionProcessor().SetIsRemoteConnection(true);
    session.Update(0.016f);
    Check(session.GetProcessor().IsRemoteConnection(),
          "a mod-driven flag survives Update when the receiver cannot supply one");
    Check(session.GetPositionProcessor().IsRemoteConnection(),
          "the same on the position processor");
}

void TestSmoothingSurvivesSettingsAssignmentInBothOrders() {
    std::cout << "Settings/smoothing ordering:\n";

    cameraunlock::PositionSettings probe = cameraunlock::PositionSettings::Symmetric(
        2.0f, 2.0f, 2.0f,
        0.31f, 0.21f, 0.41f, 0.11f,
        0.0f, 0.0f);

    // Smoothing first, then settings. This is the order a config-reload handler
    // naturally uses, and it was the unsafe one: assigning a settings struct
    // carried its own smoothing fields and silently displaced the session's.
    {
        RemoteAwareReceiver rx;
        RemoteSession session(rx);
        session.SetLocalSmoothing(0.25f);
        session.SetRemoteSmoothing(0.75f);
        session.SetPositionSettings(probe);

        Check(NearEqual(session.GetPositionSettings().local_smoothing, 0.25f),
              "smoothing-then-settings keeps local_smoothing");
        Check(NearEqual(session.GetPositionSettings().remote_smoothing, 0.75f),
              "smoothing-then-settings keeps remote_smoothing");
        Check(NearEqual(session.GetPositionSettings().limit_z, 0.41f),
              "smoothing-then-settings still applies the rest of the struct");
    }

    // Settings first, then smoothing.
    {
        RemoteAwareReceiver rx;
        RemoteSession session(rx);
        session.SetPositionSettings(probe);
        session.SetLocalSmoothing(0.25f);
        session.SetRemoteSmoothing(0.75f);

        Check(NearEqual(session.GetPositionSettings().local_smoothing, 0.25f),
              "settings-then-smoothing keeps local_smoothing");
        Check(NearEqual(session.GetPositionSettings().remote_smoothing, 0.75f),
              "settings-then-smoothing keeps remote_smoothing");
        Check(NearEqual(session.GetPositionSettings().limit_z, 0.41f),
              "settings-then-smoothing still applies the rest of the struct");
    }

    // A SetSettings straight on the processor bypasses the session entirely.
    // Update() re-asserts, so the getters cannot be left lying about the state.
    {
        RemoteAwareReceiver rx;
        RemoteSession session(rx);
        session.SetLocalSmoothing(0.25f);
        session.SetRemoteSmoothing(0.75f);
        session.GetPositionProcessor().SetSettings(probe);

        session.Update(0.016f);

        Check(NearEqual(session.GetPositionSettings().local_smoothing, 0.25f),
              "Update re-asserts local_smoothing over a direct SetSettings");
        Check(NearEqual(session.GetPositionSettings().remote_smoothing, 0.75f),
              "Update re-asserts remote_smoothing over a direct SetSettings");
        Check(NearEqual(session.GetLocalSmoothing(), 0.25f),
              "the session getter reports the effective value");
    }
}

void TestSessionSmoothingReachesBothProcessors() {
    std::cout << "Smoothing setters reach both processors:\n";

    RemoteAwareReceiver rx;
    RemoteSession session(rx);
    session.SetLocalSmoothing(0.25f);
    session.SetRemoteSmoothing(0.75f);

    Check(NearEqual(session.GetProcessor().GetLocalSmoothing(), 0.25f),
          "rotation processor got local_smoothing");
    Check(NearEqual(session.GetProcessor().GetRemoteSmoothing(), 0.75f),
          "rotation processor got remote_smoothing");
    Check(NearEqual(session.GetPositionSettings().local_smoothing, 0.25f),
          "position processor got local_smoothing");
    Check(NearEqual(session.GetPositionSettings().remote_smoothing, 0.75f),
          "position processor got remote_smoothing");
    Check(NearEqual(session.GetLocalSmoothing(), 0.25f), "session reports local_smoothing");
    Check(NearEqual(session.GetRemoteSmoothing(), 0.75f), "session reports remote_smoothing");
}

}  // namespace

int RunSessionTests() {
    std::cout << "\nHeadTrackingSession tests:\n";
    g_failures = 0;

    TestNoData();
    TestRotationFlowsThrough();
    TestStabilizationRecenter();
    TestAutoRecenterWaitsForSettle();
    TestAutoRecenterOnConnectIsOffByDefault();
    TestTrackerSideCenterLandsAtZero();
    TestModeCycling();
    TestDuplicatePacketFiltering();
    TestRecenterZeroesPose();
    TestManualRecenterDisarmsTheAutomaticOne();
    TestRemoteRecenterRequest();
    TestRecenterSeedsCurrentFrameWithCenteredPose();
    TestConnectionFlagReachesBothProcessors();
    TestLiveConnectionSwitch();
    TestSmoothingSelectionFollowsTheConnection();
    TestNonConstIsRemoteConnectionIsDetected();
    TestGracefulDegradation();
    TestSmoothingSurvivesSettingsAssignmentInBothOrders();
    TestSessionSmoothingReachesBothProcessors();

    if (g_failures == 0) {
        std::cout << "Session tests: all passed\n";
    } else {
        std::cout << "Session tests: " << g_failures << " failure(s)\n";
    }
    return g_failures;
}
