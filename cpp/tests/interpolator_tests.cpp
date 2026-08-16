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

constexpr float kDt = 1.0f / 200.0f;            // 200 fps render
constexpr float kSampleInterval = 1.0f / 60.0f; // 60 Hz tracker

// Drives a real interpolation segment: two samples to establish the interval
// and a from->to segment, a third that jumps the pose, then a stall.
//
// The segment matters. A sequence whose only new sample is the first one
// leaves from == to, so the output is that value for ANY segment position and
// the test cannot tell the fix from the bug it replaces. This one builds
// from=0 -> to=25 and then stops, which is what parks the old code on 37.5.
float PoseAfterStall(float stallSeconds) {
    cameraunlock::PoseInterpolator interp;
    interp.Update(0.0f, 0.0f, 0.0f, true, kDt);   // first sample
    interp.Update(0.0f, 0.0f, 0.0f, true, kSampleInterval);  // seeds the interval EMA
    interp.Update(25.0f, 0.0f, 0.0f, true, kSampleInterval); // the jump: from 0 -> to 25

    cameraunlock::InterpolatedPose out{};
    const int frames = static_cast<int>(stallSeconds / kDt);
    for (int i = 0; i < frames; ++i) {
        out = interp.Update(25.0f, 0.0f, 0.0f, false, kDt);
    }
    return out.yaw;
}

// A stalled tracker must settle on the pose it actually reported. The old
// behaviour clamped extrapolation progress and then held it, parking the
// output at 1.5x - a 25 deg turn rendering as 37.5 deg, forever.
void TestPoseStalledFeedSettlesOnLastSample() {
    Check(NearEqual(PoseAfterStall(3.0f), 25.0f, 0.01f),
          "a long stall settles on the reported yaw, not 1.5x it");
}

// The other half of the contract: a dropped packet or two is a LIVE feed. It
// must still extrapolate and then hold, exactly as before. Retreating here
// would drag the camera backwards against a head that is still turning.
void TestPoseDroppedPacketStillExtrapolates() {
    // 50 ms is ~3 missed samples at 60 Hz, inside an ordinary Wi-Fi burst.
    const float held = PoseAfterStall(0.05f);
    Check(NearEqual(held, 37.5f, 0.01f),
          "a dropped-packet gap still holds the extrapolated prediction");
    Check(PoseAfterStall(0.10f) >= PoseAfterStall(0.05f) - 1e-4f,
          "the output never moves backwards during the hold window");
}

void TestSegmentPositionShape() {
    cameraunlock::PoseInterpolator interp;
    const float hold = cameraunlock::PoseInterpolator::kExtrapolationHoldSeconds;
    const float decay = cameraunlock::PoseInterpolator::kExtrapolationDecaySeconds;

    Check(NearEqual(interp.SegmentPosition(0.5f, 0.0f), 0.5f),
          "mid-segment progress passes through");
    Check(NearEqual(interp.SegmentPosition(1.25f, 0.0f), 1.25f),
          "short overshoot still extrapolates");
    Check(NearEqual(interp.SegmentPosition(5.0f, hold), 1.5f),
          "progress is capped, and holds at the cap up to the hold threshold");
    Check(NearEqual(interp.SegmentPosition(5.0f, hold + decay * 0.5f), 1.25f),
          "halfway through the decay sits halfway back (smoothstep midpoint)");
    Check(NearEqual(interp.SegmentPosition(5.0f, hold + decay), 1.0f),
          "a full decay window later holds the sample itself");
    Check(NearEqual(interp.SegmentPosition(5.0f, 600.0f), 1.0f),
          "a long stall stays on the sample");
    Check(NearEqual(interp.SegmentPosition(-1.0f, 0.0f), 0.0f),
          "negative progress clamps to the segment start");

    // Smoothstep, so the decay starts and ends with zero slope - no velocity
    // step into or out of the correction.
    const float a = interp.SegmentPosition(5.0f, hold + decay * 0.01f);
    Check(NearEqual(a, 1.5f, 0.005f), "the decay enters with no velocity step");
    const float b = interp.SegmentPosition(5.0f, hold + decay * 0.99f);
    Check(NearEqual(b, 1.0f, 0.005f), "the decay leaves with no velocity step");
}

float PositionAfterStall(float stallSeconds) {
    cameraunlock::PositionInterpolator interp;
    int64_t ts = 1000;
    const int64_t step = 16666;
    interp.Update(cameraunlock::PositionData(0.0f, 0.0f, 0.0f, ts), kDt);
    ts += step;
    interp.Update(cameraunlock::PositionData(0.0f, 0.0f, 0.0f, ts), kSampleInterval);
    ts += step;
    cameraunlock::PositionData held(0.4f, 0.0f, 0.0f, ts);
    cameraunlock::PositionData out = interp.Update(held, kSampleInterval);

    const int frames = static_cast<int>(stallSeconds / kDt);
    for (int i = 0; i < frames; ++i) {
        out = interp.Update(held, kDt);
    }
    return out.x;
}

void TestPositionStall() {
    Check(NearEqual(PositionAfterStall(3.0f), 0.4f, 0.001f),
          "a long position stall settles on the reported x");
    Check(NearEqual(PositionAfterStall(0.05f), 0.6f, 0.001f),
          "a dropped-packet position gap still holds the prediction");
}

}  // namespace

int RunInterpolatorTests() {
    std::cout << "Interpolator tests:\n";
    TestPoseStalledFeedSettlesOnLastSample();
    TestPoseDroppedPacketStillExtrapolates();
    TestSegmentPositionShape();
    TestPositionStall();
    return g_failures;
}
