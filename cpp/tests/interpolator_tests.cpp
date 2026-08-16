// Tests for the interpolators' extrapolation bounds.

#include <cameraunlock/processing/pose_interpolator.h>
#include <cameraunlock/processing/position_interpolator.h>

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

// A tracker that stops producing NEW values - an app holding its last pose
// while the face is lost, or a head still enough that the samples repeat
// bit-for-bit and the session's duplicate filter suppresses them. The output
// must settle on the pose that was actually reported. Parking on the
// extrapolated overshoot (the old behaviour: 1.5x forever) swings the view
// half again past where the user's head is and leaves it there.
void TestPoseStalledFeedSettlesOnLastSample() {
    cameraunlock::PoseInterpolator interp;
    const float dt = 1.0f / 200.0f;
    const float sampleInterval = 1.0f / 60.0f;

    float acc = 0.0f;
    float last = 0.0f;
    cameraunlock::InterpolatedPose out{};
    for (int frame = 0; frame < 1200; ++frame) {
        const float t = frame * dt;
        const float target = (t > 1.0f) ? 25.0f : 0.0f;
        acc += dt;
        bool packet = false;
        if (acc >= sampleInterval) { acc -= sampleInterval; packet = true; }
        const bool isNew = packet && (target != last);
        if (packet) last = target;
        out = interp.Update(target, 0.0f, 0.0f, isNew, dt);
    }
    Check(NearEqual(out.yaw, 25.0f, 0.01f), "stalled feed settles on the reported yaw");
}

// The extrapolation itself must survive: a live feed at a steady rate should
// still be predicting ahead between samples, otherwise high-refresh displays
// get the flat spots the extrapolation exists to remove.
void TestPoseExtrapolatesBetweenLiveSamples() {
    cameraunlock::PoseInterpolator interp;
    Check(NearEqual(interp.SegmentPosition(0.5f), 0.5f), "mid-segment progress passes through");
    Check(NearEqual(interp.SegmentPosition(1.25f), 1.25f), "short overshoot still extrapolates");
    Check(NearEqual(interp.SegmentPosition(1.5f), 1.5f), "extrapolation cap is reached");
    Check(NearEqual(interp.SegmentPosition(2.0f), 1.25f), "late sample eases back toward the target");
    Check(NearEqual(interp.SegmentPosition(2.5f), 1.0f), "a full interval late holds the sample itself");
    Check(NearEqual(interp.SegmentPosition(50.0f), 1.0f), "a long stall stays on the sample");
    Check(NearEqual(interp.SegmentPosition(-1.0f), 0.0f), "negative progress clamps to the segment start");
}

void TestPositionStalledFeedSettlesOnLastSample() {
    cameraunlock::PositionInterpolator interp;
    const float dt = 1.0f / 200.0f;

    // One moving sample, then the feed stops advancing its timestamp.
    interp.Update(cameraunlock::PositionData(0.0f, 0.0f, 0.0f, 1000), dt);
    interp.Update(cameraunlock::PositionData(0.0f, 0.0f, 0.0f, 1000 + 16666), dt);
    cameraunlock::PositionData held(0.4f, 0.0f, 0.0f, 1000 + 33332);
    cameraunlock::PositionData out = interp.Update(held, dt);
    for (int i = 0; i < 600; ++i) {
        out = interp.Update(held, dt);
    }
    Check(NearEqual(out.x, 0.4f, 0.001f), "stalled position feed settles on the reported x");
}

}  // namespace

int RunInterpolatorTests() {
    std::cout << "Interpolator tests:\n";
    TestPoseStalledFeedSettlesOnLastSample();
    TestPoseExtrapolatesBetweenLiveSamples();
    TestPositionStalledFeedSettlesOnLastSample();
    return g_failures;
}
