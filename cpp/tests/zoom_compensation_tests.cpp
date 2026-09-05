// Tests for camera/zoom_compensation.h. The property that matters is not the
// arithmetic, it is the invariant the arithmetic exists to hold: the screen
// displacement a head angle or a lean produces must not change when the game
// zooms. So the checks below compute that displacement both ways and compare
// it, rather than restating the formula.

#include <cameraunlock/camera/zoom_compensation.h>

#include <cmath>
#include <iostream>

namespace {

using cameraunlock::camera::FovZoomFactor;
using cameraunlock::camera::ScaleAngleForZoom;

int g_failures = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

bool NearEqual(float a, float b, float eps = 1e-5f) {
    return std::fabs(a - b) <= eps;
}

constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

// Where an angle off the view axis lands, as a fraction of the frame from its
// centre. This is the engine's own projection, and the whole point of the
// compensation is that it comes out the same at every FOV.
float ScreenFraction(float angle_deg, float tan_half_fov) {
    return std::tan(angle_deg * kDegToRad) / (2.0f * tan_half_fov);
}

// Deus Ex: Human Revolution's three measured fields of view, as tan(vfov/2) on
// a 16:9 frame: 90 degrees horizontal walking around, 75 aiming, 45 scoped.
constexpr float kBase = 0.5625f;
constexpr float kAds = 0.4316f;
constexpr float kScope = 0.2330f;

}  // namespace

int RunZoomCompensationTests() {
    std::cout << "\n[zoom_compensation]\n";

    Check(NearEqual(FovZoomFactor(kBase, kBase), 1.0f),
          "un-zoomed is exactly 1.0, so nothing moves when nothing zooms");
    Check(FovZoomFactor(kScope, kBase) < FovZoomFactor(kAds, kBase),
          "the tighter the zoom the smaller the factor");
    Check(NearEqual(FovZoomFactor(kScope, kBase), 0.41422223f, 1e-6f),
          "the scope factor is the ratio of the two tangents");

    // The invariant, on each measured FOV and across the range a neck reaches.
    for (float angle : {2.0f, 10.0f, 25.0f, 45.0f}) {
        for (float tanFov : {kAds, kScope}) {
            const float scaled = ScaleAngleForZoom(angle, FovZoomFactor(tanFov, kBase));
            Check(NearEqual(ScreenFraction(scaled, tanFov), ScreenFraction(angle, kBase)),
                  "the scaled angle lands where the raw angle landed un-zoomed");
        }
    }

    Check(NearEqual(ScaleAngleForZoom(20.0f, 1.0f), 20.0f, 1e-4f),
          "a factor of 1 returns the angle untouched");
    Check(NearEqual(ScaleAngleForZoom(0.0f, FovZoomFactor(kScope, kBase)), 0.0f),
          "zero stays zero");
    Check(NearEqual(ScaleAngleForZoom(-25.0f, FovZoomFactor(kScope, kBase)),
                    -ScaleAngleForZoom(25.0f, FovZoomFactor(kScope, kBase))),
          "the scaling is odd, so it cannot bias one direction");

    // At the angles a head actually holds, multiplying and the tangent round
    // trip agree closely - the round trip is there for the large ones.
    const float factor = FovZoomFactor(kScope, kBase);
    Check(NearEqual(ScaleAngleForZoom(5.0f, factor), 5.0f * factor, 0.01f),
          "small angles are within a hundredth of a degree of a plain multiply");
    Check(ScaleAngleForZoom(45.0f, factor) > 45.0f * factor,
          "large angles are not, and the round trip keeps the displacement right");

    return g_failures;
}
