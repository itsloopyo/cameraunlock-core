// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
// The lean a skeleton root carries a frame late (cameraunlock/ads/deferred_rig_lean.h),
// ported from Headstart's native/gamebryo/tests/gamebryo_tests.cpp.

#include <cameraunlock/ads/deferred_rig_lean.h>

#include <cmath>
#include <iostream>

namespace {

using cameraunlock::ads::DeferredRigLean;
using V = cameraunlock::math::Vec3;

int g_failures = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

bool Near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

void TestHandOver() {
    std::cout << "DeferredRigLean:\n";
    const V forward{0, 1, 0};
    const V lean{7, 3, 2};  // across the aim (x, z) and along it (y)
    DeferredRigLean rig;
    V camera = rig.Split(lean, forward, false, false, true, 0);
    Check(Near(camera.x, 7) && Near(camera.y, 3) && Near(camera.z, 2), "at the hip the camera carries it all");
    Check(Near(rig.TakeRig(true).x, 0), "and the rig nothing");

    rig.Split(lean, forward, true, false, true, 1000);
    rig.TakeRig(true);
    rig.Split(lean, forward, true, false, true, 5000);
    const V onRig = rig.TakeRig(true);
    Check(Near(onRig.x, 7, 1e-4f) && Near(onRig.y, 0, 1e-4f) && Near(onRig.z, 2, 1e-4f) && Near(rig.Applied().x, 7, 1e-4f),
          "with the sights up the lean across the aim is the rig's from the next skeleton update");
    camera = rig.Split(lean, forward, true, false, true, 5016);
    Check(Near(camera.x + onRig.x, 7, 1e-4f) && Near(camera.y + onRig.y, 3, 1e-4f) && Near(camera.z + onRig.z, 2, 1e-4f),
          "and the camera makes up the rest: the eye has the whole lean");
    Check(Near(rig.TakeRig(false).x, 0), "third person puts nothing on the rig, whatever the sights");

    rig.Split(lean, forward, true, false, true, 5032);
    const V carried = rig.TakeRig(true);
    rig.EndFrame();
    Check(Near(carried.x, 7, 1e-4f) && Near(rig.Applied().x, 0) && Near(rig.Applied().z, 0),
          "the end of a frame leaves nothing applied");

    rig.Split(lean, forward, true, false, true, 6000);
    rig.Stop();
    Check(Near(rig.TakeRig(true).x, 0), "a stop forgets the next share");

    DeferredRigLean freeLook;
    freeLook.Split(lean, forward, true, true, true, 1000);
    freeLook.TakeRig(true);
    freeLook.Split(lean, forward, true, true, true, 5000);
    const V freeRig = freeLook.TakeRig(true);
    Check(Near(freeRig.x, 0, 1e-4f) && Near(freeRig.y, 0, 1e-4f) && Near(freeRig.z, 0, 1e-4f),
          "in free look the rig takes nothing with the sights up");
    camera = freeLook.Split(lean, forward, true, true, true, 5016);
    Check(Near(camera.x, 7, 1e-4f) && Near(camera.y, 3, 1e-4f) && Near(camera.z, 2, 1e-4f), "and the camera carries the whole lean");
}

}  // namespace

int RunDeferredRigLeanTests() {
    std::cout << "Deferred rig lean tests\n";
    TestHandOver();
    if (g_failures == 0) {
        std::cout << "Deferred rig lean tests: all passed\n";
    } else {
        std::cout << "Deferred rig lean tests: " << g_failures << " failure(s)\n";
    }
    return g_failures;
}
