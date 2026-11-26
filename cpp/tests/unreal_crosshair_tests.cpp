// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
// Tests for the UMG crosshair mover's measurement of a widget's translation
// scale (cameraunlock/unreal/umg_translation_scale.h) and its telling split
// screen players' crosshairs apart (crosshair_split.h, split_layout.h), which
// read nothing from the engine.

#include <cameraunlock/unreal/crosshair_split.h>
#include <cameraunlock/unreal/split_layout.h>
#include <cameraunlock/unreal/umg_translation_scale.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>

namespace {

using namespace cameraunlock::unreal;
using namespace cameraunlock::unreal::split;

int g_failures = 0;

void Check(bool cond, const char* what, int line) {
    if (!cond) {
        ++g_failures;
        std::cout << "  [FAIL] line " << line << ": " << what << "\n";
    }
}

#define CHECK(cond) Check((cond), #cond, __LINE__)

bool Near(double a, double b, double tolerance) { return std::fabs(a - b) < tolerance; }

constexpr double kProbe = 1000;
constexpr double kPi = 3.14159265358979323846;

// Where a painted geometry lays the probes along the widget's X and Y, for a
// layout scale of (layoutX, layoutY) above a widget with its own render scale
// and turn: Slate's scale, then turn (x cos - y sin, x sin + y cos), then the
// layout's scale.
struct Painted {
    std::array<double, 2> alongX, alongY;
};
Painted Paint(double layoutX, double layoutY, double scaleX, double scaleY, double degrees) {
    const double c = std::cos(degrees * kPi / 180), s = std::sin(degrees * kPi / 180);
    const auto lay = [&](double x, double y) {
        const double sx = x * scaleX, sy = y * scaleY;
        return std::array<double, 2>{layoutX * (sx * c - sy * s), layoutY * (sx * s + sy * c)};
    };
    return {lay(kProbe, 0), lay(0, kProbe)};
}

bool Is(const std::array<double, 2>& got, double across, double down) {
    return Near(got[0], across, 1e-9) && Near(got[1], down, 1e-9);
}

void TestUnturned() {
    // The DPI scale of a 1280 x 720 window, 0.6667, with no render scale.
    const Painted p = Paint(0.6667, 0.6667, 1, 1, 0);
    CHECK(Is(TranslationScale(p.alongX, p.alongY, kProbe, 1, 1, 0), 0.6667, 0.6667));
    // A render scale of its own is taken back out.
    const Painted scaled = Paint(0.823, 0.823, 2, 0.5, 0);
    CHECK(Is(TranslationScale(scaled.alongX, scaled.alongY, kProbe, 2, 0.5, 0), 0.823, 0.823));
    // A mirrored widget still moves the right way.
    const Painted mirrored = Paint(0.6667, 0.6667, -1, 1, 0);
    CHECK(Is(TranslationScale(mirrored.alongX, mirrored.alongY, kProbe, -1, 1, 0), 0.6667, 0.6667));
}

void TestTurned() {
    // Turned 45 degrees, unscaled.
    const Painted bow = Paint(0.6667, 0.6667, 1, 1, 45);
    CHECK(Is(TranslationScale(bow.alongX, bow.alongY, kProbe, 1, 1, 45), 0.6667, 0.6667));
    // Exactly 45 degrees reads the diagonal, so either way round gives the layout's scale.
    const Painted back = Paint(0.6667, 0.6667, 1, 1, -45);
    CHECK(Is(TranslationScale(back.alongX, back.alongY, kProbe, 1, 1, -45), 0.6667, 0.6667));
    // A layout scaled unevenly, under a widget scaled unevenly and turned a quarter.
    const Painted quarter = Paint(0.5, 0.8, 2, 0.25, 90);
    CHECK(Is(TranslationScale(quarter.alongX, quarter.alongY, kProbe, 2, 0.25, 90), 0.5, 0.8));
    // Across from the diagonal and down from the off-diagonal.
    const Painted steep = Paint(0.75, 1.25, 1.5, 0.5, 120);
    CHECK(Is(TranslationScale(steep.alongX, steep.alongY, kProbe, 1.5, 0.5, 120), 0.75, 1.25));
    const Painted slight = Paint(0.75, 1.25, 1.5, 0.5, 10);
    CHECK(Is(TranslationScale(slight.alongX, slight.alongY, kProbe, 1.5, 0.5, 10), 0.75, 1.25));
    // Turned half round, and mirrored and turned.
    const Painted half = Paint(0.6667, 0.6667, 1, 1, 180);
    CHECK(Is(TranslationScale(half.alongX, half.alongY, kProbe, 1, 1, 180), 0.6667, 0.6667));
    const Painted mirrored = Paint(0.9, 0.7, -1, 2, 30);
    CHECK(Is(TranslationScale(mirrored.alongX, mirrored.alongY, kProbe, -1, 2, 30), 0.9, 0.7));
    // An uneven scale at exactly 45 degrees.
    const Painted uneven = Paint(0.6667, 0.6667, 2, 0.5, 45);
    CHECK(Is(TranslationScale(uneven.alongX, uneven.alongY, kProbe, 2, 0.5, 45), 0.6667, 0.6667));
}

bool At(const std::array<double, 2>& p, double x, double y) { return Near(p[0], x, 1e-9) && Near(p[1], y, 1e-9); }

void TestOwningPlayer() {
    const std::array<std::uintptr_t, 4> one{0x100, 0, 0, 0};
    const std::array<std::uintptr_t, 4> two{0x100, 0x200, 0, 0};
    // One player: every crosshair is player one's, whoever owns it.
    CHECK(OwningPlayer(0x100, one) == 0);
    CHECK(OwningPlayer(0x900, one) == 0);
    CHECK(OwningPlayer(0, one) == 0);
    // Split screen: each to the player whose controller owns it.
    CHECK(OwningPlayer(0x100, two) == 0);
    CHECK(OwningPlayer(0x200, two) == 1);
    // A crosshair of no player's controller stays with player one, and an
    // owner of 0 never matches an empty slot.
    CHECK(OwningPlayer(0x900, two) == 0);
    CHECK(OwningPlayer(0, two) == 0);
}

void TestSplitScreen() {
    // One player: never split, however recently player one drew.
    CHECK(!SplitScreen(std::array<double, 4>{5000, 0, 0, 0}, 5000, 1000));
    // Player two drew within the window: split.
    CHECK(SplitScreen(std::array<double, 4>{5000, 4500, 0, 0}, 5000, 1000));
    // Player two has drawn nothing for longer than the window: no longer split.
    CHECK(!SplitScreen(std::array<double, 4>{5000, 3000, 0, 0}, 5000, 1000));
    // Player four alone after the first counts too.
    CHECK(SplitScreen(std::array<double, 4>{5000, 0, 0, 4999}, 5000, 1000));
}

void TestPlayerRect() {
    const std::array<double, 2> size{1280, 720};
    // One player: the whole viewport, wherever the axis lands (The Outer
    // Worlds' lands below the middle), as before players were told apart.
    const Rect whole = PlayerRect({640, 406.8}, {640, 406.8}, size, false);
    CHECK(Near(whole.left, 0, 1e-9) && Near(whole.top, 0, 1e-9) && Near(whole.right, 1280, 1e-9) && Near(whole.bottom, 720, 1e-9));
    // Side by side: player one's axis at the middle of the left half, player two's of the right.
    const Rect left = PlayerRect({320, 360}, {320, 360}, size, true);
    CHECK(Near(left.left, 0, 1e-9) && Near(left.right, 640, 1e-9) && Near(left.top, 0, 1e-9) && Near(left.bottom, 720, 1e-9));
    const Rect right = PlayerRect({960, 360}, {320, 360}, size, true);
    CHECK(Near(right.left, 640, 1e-9) && Near(right.right, 1280, 1e-9) && Near(right.top, 0, 1e-9) && Near(right.bottom, 720, 1e-9));
    // One above the other.
    const Rect lower = PlayerRect({640, 540}, {640, 180}, size, true);
    CHECK(Near(lower.left, 0, 1e-9) && Near(lower.right, 1280, 1e-9) && Near(lower.top, 360, 1e-9) && Near(lower.bottom, 720, 1e-9));
    // A part worked out past the viewport is cut back to it.
    const Rect past = PlayerRect({1000, 360}, {340, 360}, size, true);
    CHECK(Near(past.left, 660, 1e-9) && Near(past.right, 1280, 1e-9));
}

void TestHeldIn() {
    const Rect right{640, 0, 1280, 720};
    CHECK(At(HeldIn(right, {900, 300}), 900, 300));
    // An aim turned out of player two's half stays at that half's edge, not in player one's.
    CHECK(At(HeldIn(right, {500, 300}), 640, 300));
    CHECK(At(HeldIn(right, {1500, -20}), 1280, 0));
    // A rect turned inside out holds the mark at its first corner.
    CHECK(At(HeldIn(Rect{640, 0, 600, 720}, {500, 900}), 640, 720));
}

bool IsPart(const std::optional<cameraunlock::unreal::split::Part>& p, double x, double y, double w, double h) {
    return p && Near(p->x, x, 1e-9) && Near(p->y, y, 1e-9) && Near(p->w, w, 1e-9) && Near(p->h, h, 1e-9);
}

void TestSplitLayout() {
    namespace split = cameraunlock::unreal::split;
    using split::Four;
    using split::Three;
    using split::Two;
    const auto part = [](int index, int count, Two two = Two::Horizontal, Three three = Three::FavorTop, Four four = Four::Grid) {
        return split::PartOf(index, count, two, three, four);
    };
    CHECK(IsPart(part(0, 1), 0, 0, 1, 1));
    // Two players one above the other, and side by side as The Ascent has them.
    CHECK(IsPart(part(1, 2, Two::Horizontal), 0, 0.5, 1, 0.5));
    CHECK(IsPart(part(0, 2, Two::Vertical), 0, 0, 0.5, 1));
    CHECK(IsPart(part(1, 2, Two::Vertical), 0.5, 0, 0.5, 1));
    CHECK(IsPart(part(0, 3, Two::Vertical, Three::FavorTop), 0, 0, 1, 0.5));
    CHECK(IsPart(part(2, 3, Two::Vertical, Three::FavorTop), 0.5, 0.5, 0.5, 0.5));
    CHECK(IsPart(part(2, 3, Two::Vertical, Three::FavorBottom), 0, 0.5, 1, 0.5));
    CHECK(IsPart(part(1, 3, Two::Vertical, Three::Vertical), 1.0 / 3, 0, 1.0 / 3, 1));
    CHECK(IsPart(part(2, 3, Two::Vertical, Three::Horizontal), 0, 2.0 / 3, 1, 1.0 / 3));
    CHECK(IsPart(part(3, 4), 0.5, 0.5, 0.5, 0.5));
    CHECK(IsPart(part(2, 4, Two::Vertical, Three::FavorTop, Four::Vertical), 0.5, 0, 0.25, 1));
    CHECK(IsPart(part(1, 4, Two::Vertical, Three::FavorTop, Four::Horizontal), 0, 0.25, 1, 0.25));
    // No part for a player who is not there, five players or a layout the engine has not got.
    CHECK(!part(2, 2));
    CHECK(!part(-1, 2));
    CHECK(!part(0, 5));
    CHECK(!part(0, 2, static_cast<Two>(2)));
}

void TestToWindow() {
    namespace split = cameraunlock::unreal::split;
    double x = 0, y = 0;
    // The middle of player two's half, side by side, is three quarters across the window.
    split::ToWindow({0.5, 0, 0.5, 1}, 0, 0, x, y);
    CHECK(Near(x, 0.5, 1e-12) && Near(y, 0, 1e-12));
    // The top-left corner of the lower half is the window's left edge, half way down.
    split::ToWindow({0, 0.5, 1, 0.5}, -1, 1, x, y);
    CHECK(Near(x, -1, 1e-12) && Near(y, 0, 1e-12));
    // The whole window is the window.
    split::ToWindow({}, 0.3, -0.7, x, y);
    CHECK(Near(x, 0.3, 1e-12) && Near(y, -0.7, 1e-12));
}

}  // namespace

int RunUnrealCrosshairTests() {
    g_failures = 0;
    std::cout << "Unreal crosshair:\n";
    TestUnturned();
    TestTurned();
    TestOwningPlayer();
    TestSplitScreen();
    TestPlayerRect();
    TestHeldIn();
    TestSplitLayout();
    TestToWindow();
    std::cout << (g_failures ? "  [FAIL] " : "  [PASS] ") << "Unreal crosshair, " << g_failures << " failed\n";
    return g_failures;
}
