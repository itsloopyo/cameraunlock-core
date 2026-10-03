// Tests for the aim-down-sights transition (cameraunlock/ads/ads_fade.h), the
// fade a mod rides to ease a positional lean out while the sights are up. Every
// case here is a bug that has shipped in this shape before: a transition that
// switches instead of easing, or steps when it is reversed.

#include <cameraunlock/ads/ads_fade.h>
#include <cameraunlock/ads/aim_mode.h>
#include <cameraunlock/ads/lean_handover.h>
#include <cameraunlock/rendering/aim_marker.h>

#include <cmath>
#include <iostream>
#include <string>

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

bool Near(float a, float b, float eps = 1e-4f) {
    return std::isfinite(a) && std::fabs(a - b) <= eps;
}

using cameraunlock::ads::AdsFade;

// ---- the transition ----------------------------------------------------------

void TestFadeEndpointsAndShape() {
    std::cout << "ADS transition:\n";

    AdsFade fade;
    Check(Near(fade.Update(false, 1000), 1.0f), "the hip is full scale");

    // The frame the sights start coming up is still full scale - the transition
    // eases out of rest, it does not step.
    Check(Near(fade.Update(true, 0), 1.0f), "the entry frame does not step");
    Check(Near(fade.Update(true, AdsFade::kLowerMs / 2), 0.5f, 1e-3f),
          "smoothstep is symmetric about the half");
    Check(Near(fade.Update(true, AdsFade::kLowerMs), 0.0f), "it reaches zero by sights-up");
    Check(Near(fade.Update(true, AdsFade::kLowerMs + 5000), 0.0f),
          "and holds there for as long as the sights are up");

    Check(Near(fade.Update(false, 10000), 0.0f), "lowering the weapon does not step either");
    Check(Near(fade.Update(false, 10000 + AdsFade::kRaiseMs), 1.0f), "and it comes back in full");
}

void TestFadeIsMonotonic() {
    AdsFade fade;
    fade.Update(true, 0);
    float last = 1.1f;
    bool monotonic = true;
    for (unsigned long long t = 0; t <= AdsFade::kLowerMs; t += 5) {
        const float now = fade.Update(true, t);
        monotonic = monotonic && now <= last + 1e-4f;
        last = now;
    }
    Check(monotonic && Near(last, 0.0f), "the ride out never reverses");
}

// A player who taps aim interrupts the transition half way. It has to turn round
// from where it is, not from where it started, or the view jumps by the part
// that had already faded.
//
// The bound is equality, not a tolerance. This case previously allowed 0.55,
// which no implementation returning a value in [0,1] could violate at the half
// way point - so it passed for as long as the reversal stepped by a clean half.
void TestFadeInterruptedHalfWayDoesNotJump() {
    AdsFade fade;
    fade.Update(true, 0);
    const float half = fade.Update(true, AdsFade::kLowerMs / 2);
    const float resumed = fade.Update(false, AdsFade::kLowerMs / 2);
    Check(Near(resumed, half), "an interrupted transition is continuous");
    Check(Near(fade.Update(false, AdsFade::kLowerMs / 2 + AdsFade::kRaiseMs), 1.0f),
          "and still finishes");
}

// The worst reversal is the earliest one, and it is also the most common input
// there is: a tap releases the aim button a frame after pressing it, with the
// pose still all but fully applied.
void TestFadeTapDoesNotStepThePose() {
    AdsFade fade;
    fade.Update(true, 0);
    const float barely = fade.Update(true, 1);
    const float resumed = fade.Update(false, 1);
    Check(Near(resumed, barely), "a one-frame tap does not step the pose");
    Check(Near(fade.Update(false, 1 + AdsFade::kRaiseMs), 1.0f), "and returns to the hip");
}

// An interrupted leg travels at the same RATE as a whole one, so a short
// reversal finishes quickly rather than taking the full duration to cover a
// fraction of the distance.
void TestFadeReversalIsScaledToTheDistanceLeft() {
    AdsFade fade;
    fade.Update(true, 0);
    const float quarter = fade.Update(true, AdsFade::kLowerMs / 4);
    fade.Update(false, AdsFade::kLowerMs / 4);
    const auto remaining =
        static_cast<unsigned long long>(static_cast<float>(AdsFade::kRaiseMs) * (1.0f - quarter));
    Check(Near(fade.Update(false, AdsFade::kLowerMs / 4 + remaining + 1), 1.0f),
          "the ride back is scaled to the distance left");
}

// A clock that steps backwards must not settle the transition instantly. The
// subtraction is unsigned, so an unguarded one wraps to an enormous elapsed and
// the fade lands on its target on the spot - a snap, in the one place this class
// exists to prevent one. Clamped, the leg reports its own start until the clock
// catches up, which is a far smaller wrong answer and a recoverable one.
void TestFadeSurvivesABackwardsClock() {
    AdsFade fade;
    fade.Update(true, 1000);
    fade.Update(true, 1000 + AdsFade::kLowerMs / 2);
    Check(!Near(fade.Update(true, 999), 0.0f),
          "a backwards clock does not settle the transition");
    Check(Near(fade.Update(true, 1000 + AdsFade::kLowerMs), 0.0f),
          "and it still completes once the clock moves on");
}

void TestFadeResetReturnsToHip() {
    AdsFade fade;
    fade.Update(true, 0);
    fade.Update(true, AdsFade::kLowerMs);
    fade.Reset();
    Check(Near(fade.Update(false, 5000), 1.0f), "Reset drops straight back to the hip");
}


// ---- the lean hand-over --------------------------------------------------------

using cameraunlock::ads::LeanHandover;
using cameraunlock::ads::LeanShares;
using cameraunlock::math::Vec3;

const Vec3 kForward(0.0f, 0.0f, 1.0f);

bool NearVec(const Vec3& a, const Vec3& b, float eps = 1e-4f) {
    return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps);
}

void TestHandoverHipLeavesTheRigAlone() {
    std::cout << "Lean hand-over:\n";
    LeanHandover handover;
    const Vec3 lean(0.25f, -0.05f, 0.1f);
    const LeanShares hip = handover.Update(lean, kForward, false, false, true, 1000);
    Check(NearVec(hip.camera, lean), "at the hip the camera carries the whole lean");
    Check(NearVec(hip.rig, Vec3()), "at the hip the rig carries nothing");
    Check(!handover.Stop(), "stopping from the hip has no rig to put back");
}

void TestHandoverSightsUpMovesTheLeanToTheRig() {
    LeanHandover handover;
    const Vec3 lean(0.25f, -0.05f, 0.1f);
    handover.Update(lean, kForward, true, false, true, 0);
    const LeanShares up = handover.Update(lean, kForward, true, false, true, AdsFade::kLowerMs + 1);
    Check(NearVec(up.rig, Vec3(0.25f, -0.05f, 0.0f)), "sights up: the rig carries the whole lateral lean");
    Check(NearVec(up.camera, Vec3(0.0f, 0.0f, 0.1f)),
          "sights up: the camera, and so the aim hook and reticle, keep only the part along the aim");
}

void TestHandoverKeepsThePartAlongTheAimOnTheCamera() {
    // A pitched aim, so the split is along the aim and not along a fixed axis.
    const Vec3 forward(0.0f, 0.6f, 0.8f);
    const Vec3 sideways(0.25f, 0.0f, 0.0f);
    const Vec3 lean = sideways + forward * 0.2f;
    LeanHandover handover;
    handover.Update(lean, forward, true, false, true, 0);
    const LeanShares up = handover.Update(lean, forward, true, false, true, AdsFade::kLowerMs + 1);
    Check(NearVec(up.camera, forward * 0.2f), "leaning in along a pitched aim stays on the camera");
    Check(NearVec(up.rig, sideways), "only the lean across a pitched aim goes to the rig");

    LeanHandover noRig;
    noRig.Update(lean, forward, true, false, false, 0);
    const LeanShares eased = noRig.Update(lean, forward, true, false, false, AdsFade::kLowerMs + 1);
    Check(NearVec(eased.camera, forward * 0.2f) && NearVec(eased.rig, Vec3()),
          "with no rig the lateral lean eases out and the lean along the aim stays");
}

void TestHandoverSharesAlwaysSumToTheLean() {
    LeanHandover handover;
    const Vec3 lean(0.25f, -0.05f, 0.1f);
    bool summed = true;
    bool split = false;
    for (unsigned long long t = 0; t <= AdsFade::kLowerMs + 10; t += 5) {
        const LeanShares s = handover.Update(lean, kForward, true, false, true, t);
        summed = summed && NearVec(s.camera + s.rig, lean);
        split = split || (s.camera.x > 0.01f && s.rig.x > 0.01f);
    }
    for (unsigned long long t = 1000; t <= 1000 + AdsFade::kRaiseMs + 10; t += 5) {
        const LeanShares s = handover.Update(lean, kForward, false, false, true, t);
        summed = summed && NearVec(s.camera + s.rig, lean);
    }
    Check(split, "the hand-over passes through frames where both carriers hold part of it");
    Check(summed, "on every frame of the hand-over the eye lands where the whole lean puts it");
}

void TestHandoverTrueFreeLookKeepsTheLeanOnTheCamera() {
    LeanHandover handover;
    const Vec3 lean(0.25f, 0.0f, 0.0f);
    handover.Update(lean, kForward, true, true, true, 0);
    const LeanShares up = handover.Update(lean, kForward, true, true, true, AdsFade::kLowerMs + 1);
    Check(NearVec(up.camera, lean) && NearVec(up.rig, Vec3()),
          "true free look: the camera keeps the lean through the aim");

    // Switching to sights locked mid-aim slides the lean onto the rig, not in a step.
    const LeanShares first = handover.Update(lean, kForward, true, false, true, 1000);
    Check(Near(first.camera.x, 0.25f), "the switch does not step on its first frame");
    const LeanShares mid = handover.Update(lean, kForward, true, false, true, 1000 + AdsFade::kLowerMs / 2);
    Check(mid.camera.x > 0.01f && mid.rig.x > 0.01f, "the switch hands the lean over gradually");
}

void TestHandoverWithoutARigEasesTheLeanOut() {
    LeanHandover handover;
    const Vec3 lean(0.25f, 0.0f, 0.0f);
    handover.Update(lean, kForward, true, false, false, 0);
    const LeanShares up = handover.Update(lean, kForward, true, false, false, AdsFade::kLowerMs + 1);
    Check(NearVec(up.camera, Vec3()) && NearVec(up.rig, Vec3()),
          "with no rig to carry it the lean eases out on the sights");
    Check(!handover.Stop(), "a rig that carried nothing has nothing to put back");
}

LeanShares AimedWithStop(const Vec3& lean, bool trueFreeLook) {
    LeanHandover handover;
    handover.SetForwardStop(0.12f);
    handover.Update(lean, kForward, true, trueFreeLook, true, 0);
    return handover.Update(lean, kForward, true, trueFreeLook, true, AdsFade::kLowerMs + 1);
}

void TestHandoverForwardStopHoldsTheEyeBehindTheSights() {
    const Vec3 lean(0.1f, 0.0f, 0.3f);
    const LeanShares locked = AimedWithStop(lean, false);
    Check(Near(locked.camera.z, 0.12f), "sights up: leaning in stops at the eye relief");
    Check(Near(locked.rig.x, 0.1f), "and the lean across the aim is untouched by the stop");
    Check(Near(AimedWithStop(lean, true).camera.z, 0.12f), "the stop holds in true free look too");

    LeanHandover hip;
    hip.SetForwardStop(0.12f);
    Check(Near(hip.Update(lean, kForward, false, false, true, 0).camera.z, 0.3f), "at the hip there is no stop");

    Check(Near(AimedWithStop(Vec3(0.0f, 0.0f, -0.1f), false).camera.z, -0.1f), "leaning back is not stopped");

    LeanHandover easing;
    easing.SetForwardStop(0.12f);
    easing.Update(lean, kForward, true, false, true, 0);
    const float mid = easing.Update(lean, kForward, true, false, true, AdsFade::kLowerMs / 2).camera.z;
    Check(mid < 0.3f && mid > 0.12f, "the stop eases in with the sights");
}

void TestHandoverStopReleasesTheRig() {
    LeanHandover handover;
    const Vec3 lean(0.25f, 0.0f, 0.0f);
    handover.Update(lean, kForward, true, false, true, 0);
    handover.Update(lean, kForward, true, false, true, AdsFade::kLowerMs + 1);
    Check(handover.Stop(), "stopping with the rig holding the lean asks for it to be put back");
    Check(!handover.Stop(), "and asks once");
    const LeanShares after = handover.Update(lean, kForward, false, false, true, 5000);
    Check(NearVec(after.camera, lean) && NearVec(after.rig, Vec3()), "after a stop the next frame starts at the hip");
}

// ---- the aim mode ------------------------------------------------------------

using cameraunlock::ads::AimMarkerOpacity;
using cameraunlock::ads::AimMode;
using cameraunlock::ads::AimModeLabel;
using cameraunlock::ads::AimModePair;
using cameraunlock::ads::DecodeAimMode;
using cameraunlock::ads::EncodeAimMode;
using cameraunlock::ads::NextAimMode;

void TestAimModeDecodesThePair() {
    std::cout << "Aim mode:\n";
    Check(DecodeAimMode(false, false) == AimMode::SightsLocked, "both false, or both absent, is sights locked");
    Check(DecodeAimMode(true, false) == AimMode::TrueFreeLook,
          "TrueFreeLook alone is true free look, so a config from before the marker keeps its mode");
    Check(DecodeAimMode(false, true) == AimMode::SightsLocked, "FreeLookMarker alone is sights locked");
    Check(DecodeAimMode(true, true) == AimMode::FreeLookMarker, "both true is free look with a marker");
}

void TestAimModeCycleAndEncode() {
    Check(NextAimMode(AimMode::SightsLocked) == AimMode::FreeLookMarker &&
              NextAimMode(AimMode::FreeLookMarker) == AimMode::TrueFreeLook &&
              NextAimMode(AimMode::TrueFreeLook) == AimMode::SightsLocked,
          "the cycle is sights locked, free look with a marker, true free look and round again");

    bool roundTrips = true;
    bool markerWithoutFreeLook = false;
    AimMode mode = AimMode::SightsLocked;
    for (int step = 0; step < 6; ++step) {
        const AimModePair pair = EncodeAimMode(mode);
        roundTrips = roundTrips && DecodeAimMode(pair.trueFreeLook, pair.freeLookMarker) == mode;
        markerWithoutFreeLook = markerWithoutFreeLook || (!pair.trueFreeLook && pair.freeLookMarker);
        mode = NextAimMode(mode);
    }
    Check(roundTrips, "every mode of the cycle decodes from the pair it encodes to");
    Check(!markerWithoutFreeLook, "the cycle never writes FreeLookMarker without TrueFreeLook");

    const AimModePair marker = EncodeAimMode(AimMode::FreeLookMarker);
    const AimModePair free = EncodeAimMode(AimMode::TrueFreeLook);
    Check(marker.trueFreeLook && marker.freeLookMarker && free.trueFreeLook && !free.freeLookMarker,
          "free look with a marker is true/true and true free look is true/false");
}

void TestAimModeLabels() {
    Check(std::string(AimModeLabel(AimMode::SightsLocked)) == "Aim mode: sights locked" &&
              std::string(AimModeLabel(AimMode::FreeLookMarker)) == "Aim mode: free look with marker" &&
              std::string(AimModeLabel(AimMode::TrueFreeLook)) == "Aim mode: true free look",
          "the three labels are the fixed ones");
}

void TestAimMarkerShowsOnlyInFreeLookWithAMarker() {
    Check(Near(AimMarkerOpacity(AimMode::FreeLookMarker, 1.0f), 1.0f) &&
              Near(AimMarkerOpacity(AimMode::FreeLookMarker, 0.4f), 0.4f),
          "free look with a marker: the marker's opacity follows the sights");
    Check(AimMarkerOpacity(AimMode::FreeLookMarker, 0.0f) == 0.0f, "and it is gone at the hip");
    Check(AimMarkerOpacity(AimMode::SightsLocked, 1.0f) == 0.0f && AimMarkerOpacity(AimMode::TrueFreeLook, 1.0f) == 0.0f,
          "sights locked and true free look draw no marker with the sights up");
}

void TestAimMarkerFadesBothCrossesByItsOpacity() {
    using cameraunlock::rendering::FadeRgba;
    Check(FadeRgba(0xE6FFFFFF, 1.0f) == 0xE6FFFFFF && FadeRgba(0x99000000, 1.0f) == 0x99000000,
          "full opacity draws the marker's fixed style");
    Check(FadeRgba(0xE6FFFFFF, 0.0f) == 0x00FFFFFF, "no opacity leaves the colour and no alpha");
    Check(FadeRgba(0xE6FFFFFF, 0.5f) == 0x73FFFFFF && FadeRgba(0x99000000, 0.5f) == 0x4D000000,
          "half opacity halves the alpha of the ink and of the outline");
    Check(FadeRgba(0xE6123456, 2.0f) == 0xE6123456 && FadeRgba(0xE6123456, -1.0f) == 0x00123456,
          "an opacity outside 0..1 is held to it");
}

}  // namespace

int RunAdsTests() {
    std::cout << "Aim-down-sights tests\n";

    TestFadeEndpointsAndShape();
    TestFadeIsMonotonic();
    TestFadeInterruptedHalfWayDoesNotJump();
    TestFadeTapDoesNotStepThePose();
    TestFadeReversalIsScaledToTheDistanceLeft();
    TestFadeSurvivesABackwardsClock();
    TestFadeResetReturnsToHip();
    TestHandoverHipLeavesTheRigAlone();
    TestHandoverSightsUpMovesTheLeanToTheRig();
    TestHandoverKeepsThePartAlongTheAimOnTheCamera();
    TestHandoverSharesAlwaysSumToTheLean();
    TestHandoverTrueFreeLookKeepsTheLeanOnTheCamera();
    TestHandoverWithoutARigEasesTheLeanOut();
    TestHandoverForwardStopHoldsTheEyeBehindTheSights();
    TestHandoverStopReleasesTheRig();
    TestAimModeDecodesThePair();
    TestAimModeCycleAndEncode();
    TestAimModeLabels();
    TestAimMarkerShowsOnlyInFreeLookWithAMarker();
    TestAimMarkerFadesBothCrossesByItsOpacity();

    if (g_failures == 0) {
        std::cout << "ADS tests: all passed\n";
    } else {
        std::cout << "ADS tests: " << g_failures << " failure(s)\n";
    }
    return g_failures;
}
