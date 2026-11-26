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
using cameraunlock::ads::AimModeSettings;
using cameraunlock::ads::DecodeAimMode;
using cameraunlock::ads::EncodeAimMode;
using cameraunlock::ads::IsFreeLook;
using cameraunlock::ads::NextAimMode;
using cameraunlock::ads::StockSightsEngaged;

void TestAimModeDecodesTheThreeValues() {
    std::cout << "Aim mode:\n";
    Check(DecodeAimMode(false, false, false) == AimMode::SightsLocked, "all false, or all absent, is sights locked");
    Check(DecodeAimMode(true, false, false) == AimMode::TrueFreeLook,
          "TrueFreeLook alone is true free look, so a config from before the marker keeps its mode");
    Check(DecodeAimMode(false, true, false) == AimMode::SightsLocked, "FreeLookMarker alone is sights locked");
    Check(DecodeAimMode(true, true, false) == AimMode::FreeLookMarker,
          "TrueFreeLook with FreeLookMarker is free look with a marker");
    Check(DecodeAimMode(false, false, true) == AimMode::StockSights &&
              DecodeAimMode(true, false, true) == AimMode::StockSights &&
              DecodeAimMode(false, true, true) == AimMode::StockSights &&
              DecodeAimMode(true, true, true) == AimMode::StockSights,
          "StockSights is stock sights whatever the other two hold");
}

void TestAimModeCycleAndEncode() {
    Check(NextAimMode(AimMode::SightsLocked) == AimMode::FreeLookMarker &&
              NextAimMode(AimMode::FreeLookMarker) == AimMode::TrueFreeLook &&
              NextAimMode(AimMode::TrueFreeLook) == AimMode::StockSights &&
              NextAimMode(AimMode::StockSights) == AimMode::SightsLocked,
          "the cycle is sights locked, free look with a marker, true free look, stock sights and round again");

    bool roundTrips = true;
    bool markerWithoutFreeLook = false;
    AimMode mode = AimMode::SightsLocked;
    for (int step = 0; step < 8; ++step) {
        const AimModeSettings settings = EncodeAimMode(mode);
        roundTrips = roundTrips &&
                     DecodeAimMode(settings.trueFreeLook, settings.freeLookMarker, settings.stockSights) == mode;
        markerWithoutFreeLook = markerWithoutFreeLook || (!settings.trueFreeLook && settings.freeLookMarker);
        mode = NextAimMode(mode);
    }
    Check(roundTrips, "every mode of the cycle decodes from the values it encodes to");
    Check(!markerWithoutFreeLook, "the cycle never writes FreeLookMarker without TrueFreeLook");

    const AimModeSettings locked = EncodeAimMode(AimMode::SightsLocked);
    const AimModeSettings marker = EncodeAimMode(AimMode::FreeLookMarker);
    const AimModeSettings free = EncodeAimMode(AimMode::TrueFreeLook);
    const AimModeSettings stock = EncodeAimMode(AimMode::StockSights);
    Check(!locked.trueFreeLook && !locked.freeLookMarker && !locked.stockSights, "sights locked is false/false/false");
    Check(marker.trueFreeLook && marker.freeLookMarker && !marker.stockSights,
          "free look with a marker is true/true/false");
    Check(free.trueFreeLook && !free.freeLookMarker && !free.stockSights, "true free look is true/false/false");
    Check(!stock.trueFreeLook && !stock.freeLookMarker && stock.stockSights, "stock sights is false/false/true");
}

void TestAimModeLabels() {
    Check(std::string(AimModeLabel(AimMode::SightsLocked)) == "Aim mode: sights locked" &&
              std::string(AimModeLabel(AimMode::FreeLookMarker)) == "Aim mode: free look with marker" &&
              std::string(AimModeLabel(AimMode::TrueFreeLook)) == "Aim mode: true free look" &&
              std::string(AimModeLabel(AimMode::StockSights)) == "Aim mode: stock sights",
          "the four labels are the fixed ones");
}

void TestOnlyTheTwoFreeLookModesAreFreeLook() {
    Check(IsFreeLook(AimMode::FreeLookMarker) && IsFreeLook(AimMode::TrueFreeLook),
          "both free look modes are free look");
    Check(!IsFreeLook(AimMode::SightsLocked) && !IsFreeLook(AimMode::StockSights),
          "sights locked and stock sights are not, so a lean on its way out is handed over as sights locked");
}

void TestAimMarkerShowsOnlyInFreeLookWithAMarker() {
    Check(Near(AimMarkerOpacity(AimMode::FreeLookMarker, 1.0f), 1.0f) &&
              Near(AimMarkerOpacity(AimMode::FreeLookMarker, 0.4f), 0.4f),
          "free look with a marker: the marker's opacity follows the sights");
    Check(AimMarkerOpacity(AimMode::FreeLookMarker, 0.0f) == 0.0f, "and it is gone at the hip");
    Check(AimMarkerOpacity(AimMode::SightsLocked, 1.0f) == 0.0f &&
              AimMarkerOpacity(AimMode::TrueFreeLook, 1.0f) == 0.0f &&
              AimMarkerOpacity(AimMode::StockSights, 1.0f) == 0.0f,
          "sights locked, true free look and stock sights draw no marker with the sights up");
}

// ---- stock sights ------------------------------------------------------------

void TestStockSightsEngagesOnlyInItsModeWithTheSightsUp() {
    std::cout << "Stock sights:\n";
    Check(StockSightsEngaged(AimMode::StockSights, true), "stock sights with the sights up eases the pose out");
    Check(!StockSightsEngaged(AimMode::StockSights, false), "at the hip it does not");
    Check(!StockSightsEngaged(AimMode::SightsLocked, true) && !StockSightsEngaged(AimMode::FreeLookMarker, true) &&
              !StockSightsEngaged(AimMode::TrueFreeLook, true),
          "and no other mode does with the sights up");
}

// The pose as a mod applies it: a second AdsFade fed StockSightsEngaged, whose
// output scales yaw, pitch and the three lean axes and never roll.
struct Pose {
    float yaw, pitch, roll, x, y, z;
};

constexpr Pose kHead{20.0f, -8.0f, 6.0f, 0.25f, -0.05f, 0.1f};

Pose StockSightsPose(AdsFade& fade, AimMode mode, bool aiming, unsigned long long nowMs) {
    const float share = fade.Update(StockSightsEngaged(mode, aiming), nowMs);
    return {kHead.yaw * share, kHead.pitch * share, kHead.roll, kHead.x * share, kHead.y * share, kHead.z * share};
}

bool NearPose(const Pose& p, float share) {
    return Near(p.yaw, kHead.yaw * share) && Near(p.pitch, kHead.pitch * share) && p.roll == kHead.roll &&
           Near(p.x, kHead.x * share) && Near(p.y, kHead.y * share) && Near(p.z, kHead.z * share);
}

void TestStockSightsEasesEverythingButRollOut() {
    AdsFade fade;
    Check(NearPose(StockSightsPose(fade, AimMode::StockSights, false, 1000), 1.0f),
          "at the hip the pose passes through untouched");

    StockSightsPose(fade, AimMode::StockSights, true, 2000);
    const Pose mid = StockSightsPose(fade, AimMode::StockSights, true, 2000 + AdsFade::kLowerMs / 2);
    Check(Near(mid.yaw, 10.0f, 1e-2f) && Near(mid.pitch, -4.0f, 1e-2f) && Near(mid.x, 0.125f, 1e-3f) &&
              Near(mid.y, -0.025f, 1e-3f) && Near(mid.z, 0.05f, 1e-3f) && mid.roll == kHead.roll,
          "mid-transition yaw, pitch and the lean are scaled by the fade and roll is untouched");

    const Pose up = StockSightsPose(fade, AimMode::StockSights, true, 2000 + AdsFade::kLowerMs);
    Check(up.yaw == 0.0f && up.pitch == 0.0f && up.x == 0.0f && up.y == 0.0f && up.z == 0.0f && up.roll == kHead.roll,
          "sights up: yaw, pitch and all three lean axes are zero and roll is the tracker's roll");

    StockSightsPose(fade, AimMode::StockSights, false, 5000);
    Check(NearPose(StockSightsPose(fade, AimMode::StockSights, false, 5000 + AdsFade::kRaiseMs), 1.0f),
          "and the pose comes back in full as the sights go down");
}

void TestStockSightsReversalsContinue() {
    AdsFade button;
    StockSightsPose(button, AimMode::StockSights, true, 0);
    const Pose half = StockSightsPose(button, AimMode::StockSights, true, AdsFade::kLowerMs / 2);
    const Pose released = StockSightsPose(button, AimMode::StockSights, false, AdsFade::kLowerMs / 2);
    Check(Near(released.yaw, half.yaw) && Near(released.x, half.x),
          "releasing the aim button mid-transition does not step");

    // The mode key pressed with the sights up: into stock sights, then out of it.
    AdsFade key;
    Check(NearPose(StockSightsPose(key, AimMode::TrueFreeLook, true, 0), 1.0f),
          "true free look with the sights up is the whole pose");
    Check(NearPose(StockSightsPose(key, AimMode::StockSights, true, 100), 1.0f),
          "the frame the key steps into stock sights does not step");
    const Pose easing = StockSightsPose(key, AimMode::StockSights, true, 100 + AdsFade::kLowerMs / 2);
    Check(easing.yaw < kHead.yaw && easing.yaw > 0.0f, "the pose then eases out");
    const Pose steppedOut = StockSightsPose(key, AimMode::SightsLocked, true, 100 + AdsFade::kLowerMs / 2);
    Check(Near(steppedOut.yaw, easing.yaw) && Near(steppedOut.z, easing.z),
          "stepping out of stock sights mid-transition does not step");
    Check(NearPose(StockSightsPose(key, AimMode::SightsLocked, true,
                                   100 + AdsFade::kLowerMs / 2 + AdsFade::kRaiseMs),
                   1.0f),
          "and the pose eases back in full with the sights still up");
}

void TestTheOtherModesPassThePoseThrough() {
    bool untouched = true;
    for (AimMode mode : {AimMode::SightsLocked, AimMode::FreeLookMarker, AimMode::TrueFreeLook}) {
        AdsFade fade;
        untouched = untouched && NearPose(StockSightsPose(fade, mode, false, 0), 1.0f);
        untouched = untouched && NearPose(StockSightsPose(fade, mode, true, 100), 1.0f);
        untouched = untouched && NearPose(StockSightsPose(fade, mode, true, 100 + AdsFade::kLowerMs), 1.0f);
        untouched = untouched && NearPose(StockSightsPose(fade, mode, false, 1000), 1.0f);
    }
    Check(untouched, "in the other three modes the pose passes through untouched, sights up or down");
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

void TestAimMarkDrawsAnOutlineUnderTheInk() {
    using namespace cameraunlock::rendering;
    const AimMarkerStyle style;
    OverlayDrawList hidden(1920.0f, 1080.0f);
    DrawAimMark(hidden, 960.0f, 540.0f, style, 0.0f);
    Check(hidden.TriVerts().empty(), "a mark with no opacity draws nothing");

    OverlayDrawList drawn(1920.0f, 1080.0f);
    DrawAimMark(drawn, 960.0f, 540.0f, style, 0.5f);
    const auto& verts = drawn.TriVerts();
    // Two crosses of four arms, each arm a quad of two triangles.
    Check(verts.size() == 48, "the mark is an outline cross and an ink cross");
    bool outlineFirst = verts.size() == 48;
    for (std::size_t i = 0; outlineFirst && i < 24; ++i) outlineFirst = verts[i].color == FadeRgba(style.outline, 0.5f);
    for (std::size_t i = 24; outlineFirst && i < 48; ++i) outlineFirst = verts[i].color == FadeRgba(style.ink, 0.5f);
    Check(outlineFirst, "the outline is drawn first and the ink over it, both at the mark's opacity");
    float left = 1e9f, right = -1e9f, top = 1e9f, bottom = -1e9f;
    for (const OverlayVertex& v : verts) {
        left = v.x < left ? v.x : left;
        right = v.x > right ? v.x : right;
        top = v.y < top ? v.y : top;
        bottom = v.y > bottom ? v.y : bottom;
    }
    Check(Near(left + right, 2.0f * 960.0f, 1e-2f) && Near(top + bottom, 2.0f * 540.0f, 1e-2f), "the mark is centred on the pixel it was given");
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
    TestAimModeDecodesTheThreeValues();
    TestAimModeCycleAndEncode();
    TestAimModeLabels();
    TestOnlyTheTwoFreeLookModesAreFreeLook();
    TestAimMarkerShowsOnlyInFreeLookWithAMarker();
    TestStockSightsEngagesOnlyInItsModeWithTheSightsUp();
    TestStockSightsEasesEverythingButRollOut();
    TestStockSightsReversalsContinue();
    TestTheOtherModesPassThePoseThrough();
    TestAimMarkerFadesBothCrossesByItsOpacity();
    TestAimMarkDrawsAnOutlineUnderTheInk();

    if (g_failures == 0) {
        std::cout << "ADS tests: all passed\n";
    } else {
        std::cout << "ADS tests: " << g_failures << " failure(s)\n";
    }
    return g_failures;
}
