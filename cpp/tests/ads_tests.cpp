// Tests for the aim-down-sights transition (cameraunlock/ads/ads_fade.h), the
// fade a mod rides to ease a positional lean out while the sights are up. Every
// case here is a bug that has shipped in this shape before: a transition that
// switches instead of easing, or steps when it is reversed.

#include <cameraunlock/ads/ads_fade.h>

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

    if (g_failures == 0) {
        std::cout << "ADS tests: all passed\n";
    } else {
        std::cout << "ADS tests: " << g_failures << " failure(s)\n";
    }
    return g_failures;
}
