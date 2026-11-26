// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
// FOV axis conversion and rescaling (cameraunlock/camera/fov_axis.h) and the
// zoom reference (cameraunlock/camera/zoom_reference.h), ported from
// Headstart's native/common/tests/common_tests.cpp.

#include <cameraunlock/camera/fov_axis.h>
#include <cameraunlock/camera/zoom_reference.h>

#include <cmath>
#include <iostream>

namespace {

using cameraunlock::camera::FovAxis;
using cameraunlock::camera::ZoomReference;
namespace fov = cameraunlock::camera::fov;

int g_failures = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

bool Near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }

void TestFovAxis() {
    std::cout << "FOV axis:\n";
    Check(Near(fov::FromVertical(60, FovAxis::Horizontal, std::sqrt(3.0)), 90),
          "60 vertical is 90 horizontal at an aspect of sqrt(3)");
    Check(Near(fov::ToVertical(90, FovAxis::Horizontal, std::sqrt(3.0)), 60), "and back");
    Check(Near(fov::FromVertical(60, FovAxis::Vertical, 2.0), 60) && Near(fov::ToVertical(60, FovAxis::Vertical, 2.0), 60),
          "a vertical engine takes the vertical FOV as it is");

    Check(Near(fov::Rescale(70, 70, 80, FovAxis::Vertical, 16.0 / 9), 80), "the un-zoomed FOV is drawn at the target");
    Check(Near(fov::Rescale(90, 90, 60, FovAxis::Horizontal, std::sqrt(3.0)), 90), "on the engine's axis");
    Check(Near(fov::Rescale(90, 90, 70, FovAxis::Horizontal, std::sqrt(3.0)),
               fov::FromVertical(70, FovAxis::Horizontal, std::sqrt(3.0))),
          "a vertical target is carried to a horizontal engine");
    const double scoped = fov::Rescale(std::atan(std::tan(35 * 2 * fov::kHalfDegToRad) / 2) / fov::kHalfDegToRad, 70, 90,
                                       FovAxis::Vertical, 1.0);
    Check(Near(std::tan(scoped * fov::kHalfDegToRad), std::tan(45 * 2 * fov::kHalfDegToRad) / 2),
          "a 2x scope stays 2x: the tangent ratio to the base is kept");
    Check(fov::Rescale(70, 70, 0, FovAxis::Vertical, 1.0) == 70, "a target of 0 leaves the game's own");
    Check(fov::Rescale(70, 0, 90, FovAxis::Vertical, 1.0) == 70, "no base leaves the game's own");
    Check(fov::Rescale(70, 70, 90, FovAxis::Vertical, 0) == 70, "no aspect leaves the game's own");
    Check(std::isnan(fov::Rescale(std::nan(""), 70, 90, FovAxis::Vertical, 1.0)), "a game FOV that is not a number is handed back");
    Check(fov::Rescale(178, 60, 140, FovAxis::Vertical, 1.0) == 179.0, "never wider than 179");
}

void TestZoomReference() {
    std::cout << "ZoomReference:\n";
    ZoomReference z;
    Check(z.Base() == 0, "0 before anything is counted");
    z.Add(90, 60);
    z.Add(103.5, 20);
    Check(Near(z.Base(), 90), "a sprint's wider FOV does not take over");
    z.Add(40, 30);
    Check(Near(z.Base(), 90), "nor does a scope's narrower one");
    z.Add(100, 400);
    Check(Near(z.Base(), 100), "a changed setting is taken up once it outweighs the old");
    z.Add(0, 1000);
    z.Add(60, 0);
    Check(Near(z.Base(), 100), "a FOV or a time that is not positive counts for nothing");
}

}  // namespace

int RunFovAxisTests() {
    std::cout << "FOV axis tests\n";
    TestFovAxis();
    TestZoomReference();
    if (g_failures == 0) {
        std::cout << "FOV axis tests: all passed\n";
    } else {
        std::cout << "FOV axis tests: " << g_failures << " failure(s)\n";
    }
    return g_failures;
}
