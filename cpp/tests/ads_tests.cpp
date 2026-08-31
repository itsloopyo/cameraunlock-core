// Tests for the shared aim-down-sights module (cameraunlock/ads/).
//
// Three headers, and every case here is a bug that has shipped in this shape
// before: the value strings a settings file has to keep meaning the same thing
// across releases, the transition that must ease rather than switch, and the
// entry pose whose seam and capture timing are invisible from any settings or
// gate test a mod can write.

#include <cameraunlock/ads/ads_blend.h>
#include <cameraunlock/ads/ads_fade.h>
#include <cameraunlock/ads/ads_mode.h>
#include <cameraunlock/ads/entry_pose.h>

#include <cmath>
#include <cstring>
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

bool Near(float a, float b, float eps = 1e-4f) {
    return std::isfinite(a) && std::fabs(a - b) <= eps;
}

using cameraunlock::ads::AdsEntryPose;
using cameraunlock::ads::AdsFade;
using cameraunlock::ads::AdsMode;

// ---- the cycle ---------------------------------------------------------------

void TestCycleOrderAndStrings() {
    using namespace cameraunlock::ads;
    std::cout << "ADS cycle:\n";

    Check(kDefaultAdsMode == AdsMode::Paused, "the default is the mode that cannot be wrong");
    Check(NextAdsMode(AdsMode::Paused) == AdsMode::Marker
          && NextAdsMode(AdsMode::Marker) == AdsMode::Tracked
          && NextAdsMode(AdsMode::Tracked) == AdsMode::Paused,
          "three-slot cycle walks paused -> marker -> tracked -> paused");
    Check(NextAdsModeTwoSlot(AdsMode::Paused) == AdsMode::Tracked
          && NextAdsModeTwoSlot(AdsMode::Tracked) == AdsMode::Paused,
          "two-slot cycle never reaches marker");

    Check(std::strcmp(AdsModeValue(AdsMode::Paused), "paused") == 0
          && std::strcmp(AdsModeValue(AdsMode::Marker), "marker") == 0
          && std::strcmp(AdsModeValue(AdsMode::Tracked), "tracked") == 0,
          "config values are the cross-mod strings");
    Check(std::strcmp(AdsModeToast(AdsMode::Paused), "ADS: tracking paused") == 0
          && std::strcmp(AdsModeToast(AdsMode::Marker),
                         "ADS: tracking on, aim marker shown") == 0
          && std::strcmp(AdsModeToast(AdsMode::Tracked),
                         "ADS: tracking on, no aim marker") == 0,
          "toasts are the cross-mod strings");
    // Checked here as well as in the C# suite. A string pinned on one side only
    // is a string the two halves can drift apart on, which is the one thing
    // this file exists to prevent.
    Check(std::strcmp(AdsModeLabel(AdsMode::Paused), "Tracking paused") == 0
          && std::strcmp(AdsModeLabel(AdsMode::Marker),
                         "Tracking on, aim marker shown") == 0
          && std::strcmp(AdsModeLabel(AdsMode::Tracked),
                         "Tracking on, no aim marker") == 0,
          "panel labels are the cross-mod strings");
}

void TestParseRoundTripsAndFallsBack() {
    using namespace cameraunlock::ads;
    std::cout << "ADS setting parsing:\n";

    bool roundTrips = true;
    for (const AdsMode mode : { AdsMode::Paused, AdsMode::Marker, AdsMode::Tracked }) {
        roundTrips = roundTrips && ParseAdsMode(AdsModeValue(mode)) == mode;
    }
    Check(roundTrips, "every value the file can hold round-trips");
    Check(ParseAdsMode("  tracked \r\n") == AdsMode::Tracked, "surrounding whitespace is trimmed");
    Check(ParseAdsMode("MARKER") == AdsMode::Marker, "matching is case-insensitive");
    Check(ParseAdsMode(nullptr) == kDefaultAdsMode, "a missing value is the default");
    Check(ParseAdsMode("") == kDefaultAdsMode, "an empty value is the default");
    // The migration path: a mode renamed since an older release wrote the file
    // must land on stock ADS, not on whichever branch happens to be last.
    Check(ParseAdsMode("snap") == kDefaultAdsMode, "an unknown value is the default");
    Check(ParseAdsMode("trackedish") == kDefaultAdsMode, "a prefix match is not a match");
    // A two-slot mod must not accept a three-slot sibling's setting.
    Check(ParseAdsMode("marker", /*allowMarker=*/false) == kDefaultAdsMode,
          "a two-slot mod reads marker as the default");
}

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

// ---- the entry pose ----------------------------------------------------------

AdsEntryPose::Pose MakePose(float pitch, float yaw, float roll,
                            float x = 0.0f, float y = 0.0f, float z = 0.0f) {
    AdsEntryPose::Pose p;
    p.pitch = pitch;
    p.yaw = yaw;
    p.roll = roll;
    p.x = x;
    p.y = y;
    p.z = z;
    return p;
}

void TestEntryPoseHipAndEntryFrame() {
    std::cout << "ADS entry pose:\n";

    AdsEntryPose entry;
    const auto hip = entry.Relative(false, true, MakePose(5.0f, -12.0f, 3.0f, 1, 2, 3));
    Check(Near(hip.pitch, 5.0f) && Near(hip.yaw, -12.0f) && Near(hip.roll, 3.0f)
              && Near(hip.x, 1.0f) && !entry.HasEntry(),
          "hip fire passes the absolute pose straight through");

    const auto first = entry.Relative(true, true, MakePose(20.0f, -35.0f, 0.0f, 4, 5, 6));
    Check(entry.HasEntry() && Near(first.pitch, 0.0f) && Near(first.yaw, 0.0f)
              && Near(first.x, 0.0f) && Near(first.y, 0.0f) && Near(first.z, 0.0f),
          "the entry frame is identity, which is what puts the view on the aim point");
}

// Roll moves no aim point, so zeroing it would yank a head tilt the player is
// actively holding back to level and lean it in again as they move.
void TestEntryPoseRollStaysAbsolute() {
    AdsEntryPose entry;
    const auto first = entry.Relative(true, true, MakePose(0, 0, 14.0f));
    const auto later = entry.Relative(true, true, MakePose(0, 0, -6.0f));
    Check(Near(first.roll, 14.0f) && Near(later.roll, -6.0f), "roll is never made relative");
}

// Yaw arrives wrapped into -180..180, so a plain subtraction reads a 10 degree
// move across the seam as -350 and whips the view a full turn the wrong way.
void TestEntryPoseYawCrossesTheSeamTheShortWay() {
    AdsEntryPose up;
    up.Relative(true, true, MakePose(0, 175.0f, 0));
    const auto crossed = up.Relative(true, true, MakePose(0, -175.0f, 0));

    AdsEntryPose down;
    down.Relative(true, true, MakePose(0, -175.0f, 0));
    const auto back = down.Relative(true, true, MakePose(0, 175.0f, 0));

    Check(Near(crossed.yaw, 10.0f) && Near(back.yaw, -10.0f),
          "yaw crosses the -180/180 seam the short way");
}

void TestEntryPosePitchAndPositionAreRelative() {
    AdsEntryPose entry;
    entry.Relative(true, true, MakePose(10.0f, 0, 0, 3.0f, -1.0f, 2.0f));
    const auto out = entry.Relative(true, true, MakePose(-5.0f, 0, 0, 4.0f, -3.0f, 2.5f));
    Check(Near(out.pitch, -15.0f) && Near(out.x, 1.0f) && Near(out.y, -2.0f)
              && Near(out.z, 0.5f),
          "pitch and position go relative");
}

// The path that hits this: aim, open a menu, move your head, close it with the
// sights still up. Capturing on a dead frame would hold the whole aim at a
// pre-suppression offset.
void TestEntryPoseCaptureWaitsForALiveRotation() {
    AdsEntryPose entry;
    entry.Relative(true, /*live=*/false, MakePose(9.0f, 9.0f, 0));
    const bool notYet = !entry.HasEntry();
    const auto out = entry.Relative(true, /*live=*/true, MakePose(30.0f, -20.0f, 0));
    Check(notYet && entry.HasEntry() && Near(out.pitch, 0.0f) && Near(out.yaw, 0.0f),
          "the entry pose is captured from a live rotation, never from a dead frame");
}

void TestEntryPoseLoweringTheWeaponDropsIt() {
    AdsEntryPose entry;
    entry.Relative(true, true, MakePose(10.0f, 40.0f, 0));
    const auto down = entry.Relative(false, true, MakePose(12.0f, 45.0f, 0));
    const bool dropped = !entry.HasEntry() && Near(down.yaw, 45.0f);
    const auto again = entry.Relative(true, true, MakePose(12.0f, 45.0f, 0));
    Check(dropped && Near(again.yaw, 0.0f),
          "lowering the weapon drops the entry pose, so the next aim re-enters clean");
}


// ---- the blend ---------------------------------------------------------------
//
// What the fade does to the frame's pose. The one rule that is not obvious from
// either end of it is that ROLL is not in the fade at all: a head tilt moves
// neither the eye off the barrel nor the aim off the middle of the frame, and
// levelling it every time the sights come up is two horizon jolts per aim for
// nothing. It shipped faded in `paused` and was reported from the chair as roll
// switching off in a Titan the moment the sights came up.

void TestBlendHipIsTheHeadPose() {
    using cameraunlock::ads::BlendAdsPose;
    std::cout << "ADS blend:\n";

    // The two poses carry DIFFERENT rolls throughout this section. With them
    // equal - which is how these cases were first written - a blend that reads
    // roll from the relative pose, or folds it into the fade, passes every one
    // of them.
    const auto absolute = MakePose(5.0f, -12.0f, 3.0f, 1.0f, 2.0f, 3.0f);
    const auto relative = MakePose(0.0f, 0.0f, 7.0f);
    bool ok = true;
    for (const AdsMode mode : { AdsMode::Paused, AdsMode::Marker, AdsMode::Tracked }) {
        const auto out = BlendAdsPose(mode, 1.0f, absolute, relative);
        ok = ok && Near(out.pitch, 5.0f) && Near(out.yaw, -12.0f) && Near(out.roll, 3.0f)
             && Near(out.z, 3.0f);
    }
    Check(ok, "at the hip every mode is the head pose untouched");
}

// Sights fully up in `paused`: the view is the game's again, apart from the tilt
// the player is holding.
void TestBlendPausedKeepsRollAndDropsTheRest() {
    using cameraunlock::ads::BlendAdsPose;
    const auto absolute = MakePose(5.0f, -12.0f, 3.0f, 1.0f, 2.0f, 3.0f);
    const auto out = BlendAdsPose(AdsMode::Paused, 0.0f, absolute, MakePose(0, 0, 7.0f));
    Check(Near(out.pitch, 0.0f) && Near(out.yaw, 0.0f) && Near(out.x, 0.0f)
              && Near(out.y, 0.0f) && Near(out.z, 0.0f) && Near(out.roll, 3.0f),
          "paused drops rotation and lean but never the head tilt");
}

// And halfway through the fade the tilt is still whole - it does not sag toward
// level and come back.
void TestBlendPausedDoesNotFadeRollThroughTheTransition() {
    using cameraunlock::ads::BlendAdsPose;
    const auto absolute = MakePose(8.0f, -20.0f, 3.0f, 0.0f, 0.0f, 4.0f);
    const auto out = BlendAdsPose(AdsMode::Paused, 0.5f, absolute, MakePose(0, 0, 7.0f));
    Check(Near(out.pitch, 4.0f) && Near(out.yaw, -10.0f) && Near(out.z, 2.0f)
              && Near(out.roll, 3.0f),
          "half way through the fade the tilt is still whole");
}

// The tracked modes land on the entry-relative pose, whose roll is the absolute
// one already - so the two branches agree about roll and about nothing else.
void TestBlendTrackedLandsOnTheEntryRelativePose() {
    using cameraunlock::ads::BlendAdsPose;
    const auto absolute = MakePose(5.0f, -12.0f, 3.0f, 1.0f, 2.0f, 3.0f);
    const auto relative = MakePose(2.0f, -4.0f, 7.0f, 0.5f, 0.5f, 1.0f);
    bool ok = true;
    for (const AdsMode mode : { AdsMode::Marker, AdsMode::Tracked }) {
        const auto out = BlendAdsPose(mode, 0.0f, absolute, relative);
        ok = ok && Near(out.pitch, 2.0f) && Near(out.yaw, -4.0f) && Near(out.x, 0.5f)
             && Near(out.z, 1.0f) && Near(out.roll, 3.0f);
    }
    Check(ok, "the tracked modes land on the entry-relative pose, roll excepted");
}

// Mid-fade, which is the only place the interpolation curve itself is
// observable. Tested at the two endpoints alone, ANY curve passes.
void TestBlendTrackedIsLinearInTheScale() {
    using cameraunlock::ads::BlendAdsPose;
    const auto absolute = MakePose(5.0f, -12.0f, 3.0f, 1.0f, 2.0f, 3.0f);
    const auto relative = MakePose(2.0f, -4.0f, 7.0f, 0.5f, 0.5f, 1.0f);
    const auto out = BlendAdsPose(AdsMode::Tracked, 0.25f, absolute, relative);
    Check(Near(out.pitch, 5.0f * 0.25f + 2.0f * 0.75f)
              && Near(out.yaw, -12.0f * 0.25f + -4.0f * 0.75f)
              && Near(out.x, 1.0f * 0.25f + 0.5f * 0.75f)
              && Near(out.roll, 3.0f),
          "the tracked blend is linear in the scale, and roll is still untouched");
}

// The gate rule, so a mod spells it by name rather than re-deriving which mode
// stands tracking down.
void TestOnlyPausedSuspendsTracking() {
    using cameraunlock::ads::AdsSuspendsTracking;
    Check(AdsSuspendsTracking(AdsMode::Paused) && !AdsSuspendsTracking(AdsMode::Marker)
              && !AdsSuspendsTracking(AdsMode::Tracked),
          "only paused closes the tracking gate");
}

}  // namespace

int RunAdsTests() {
    std::cout << "Aim-down-sights tests\n";

    TestCycleOrderAndStrings();
    TestParseRoundTripsAndFallsBack();
    TestFadeEndpointsAndShape();
    TestFadeIsMonotonic();
    TestFadeInterruptedHalfWayDoesNotJump();
    TestFadeTapDoesNotStepThePose();
    TestFadeReversalIsScaledToTheDistanceLeft();
    TestFadeSurvivesABackwardsClock();
    TestFadeResetReturnsToHip();
    TestEntryPoseHipAndEntryFrame();
    TestEntryPoseRollStaysAbsolute();
    TestEntryPoseYawCrossesTheSeamTheShortWay();
    TestEntryPosePitchAndPositionAreRelative();
    TestEntryPoseCaptureWaitsForALiveRotation();
    TestEntryPoseLoweringTheWeaponDropsIt();
    TestBlendHipIsTheHeadPose();
    TestBlendPausedKeepsRollAndDropsTheRest();
    TestBlendPausedDoesNotFadeRollThroughTheTransition();
    TestBlendTrackedLandsOnTheEntryRelativePose();
    TestBlendTrackedIsLinearInTheScale();
    TestOnlyPausedSuspendsTracking();

    if (g_failures == 0) {
        std::cout << "ADS tests: all passed\n";
    } else {
        std::cout << "ADS tests: " << g_failures << " failure(s)\n";
    }
    return g_failures;
}
