// Tests for the REFramework lean that the rig carries a frame late
// (cameraunlock/reframework/rig_lean.h). The frames are simulated the way the
// pipeline runs them: the rig is written at LateUpdateBehavior with the request
// the previous BeginRendering made, and the game's eye carries it.

#include <cameraunlock/reframework/rig_lean.h>

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

using cameraunlock::ads::AdsFade;
using cameraunlock::camera::LeanObstruction;
using cameraunlock::math::Vec3;
using cameraunlock::reframework::RigLean;
using cameraunlock::reframework::RigLeanFrame;

bool NearVec(const Vec3& a, const Vec3& b, float eps = 1e-4f) {
    return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps && std::fabs(a.z - b.z) <= eps;
}

// A player standing at `body` whose rig is written the way the pipeline writes it.
struct Sim {
    RigLean lean;
    Vec3 body{10.0f, 1.6f, -4.0f};
    Vec3 rigApplied;
    Vec3 rigRequest;
    int rigWrites = 0;
    cameraunlock::camera::LeanQueryFn query = nullptr;
    void* queryContext = nullptr;
    // The aim axis of PitchedLean's camera, pitched 35 degrees down.
    Vec3 aimForward{0.0f, -0.5735764f, 0.8191520f};

    // Returns the eye the player sees this frame.
    Vec3 Frame(const Vec3& worldLean, bool aiming, bool trueFreeLook, unsigned long long nowMs,
               bool rigAvailable = true, RigLeanFrame* out = nullptr) {
        rigApplied = rigRequest;
        if (rigApplied.SqrMagnitude() > 0.0f) ++rigWrites;
        const Vec3 gameEye = body + rigApplied;
        const RigLeanFrame f = lean.Update(gameEye, worldLean, aimForward, rigApplied, aiming, trueFreeLook,
                                           rigAvailable, 1.0f / 60.0f, nowMs, query, queryContext);
        rigRequest = f.rigRequest;
        if (out) *out = f;
        return gameEye + f.camera;
    }

    void Stop() {
        lean.Stop();
        rigRequest = Vec3();
    }
};

// A lean built the way the pipeline builds it: view-space x, y, z through the
// clean camera's axes, here pitched 35 degrees down.
Vec3 PitchedLean(float x, float y, float z) {
    const float p = 35.0f * 3.14159265f / 180.0f;
    const Vec3 right(1.0f, 0.0f, 0.0f);
    const Vec3 up(0.0f, std::cos(p), std::sin(p));
    const Vec3 back(0.0f, -std::sin(p), std::cos(p));
    return right * x + up * y + back * z;
}

void TestHipNeverWritesTheRig() {
    std::cout << "Rig lean:\n";
    Sim sim;
    const Vec3 lean = PitchedLean(0.2f, 0.05f, -0.1f);
    bool eyeRight = true;
    RigLeanFrame f;
    for (unsigned long long t = 0; t < 2000; t += 16) {
        eyeRight = eyeRight && NearVec(sim.Frame(lean, false, false, t, true, &f), sim.body + lean);
    }
    Check(eyeRight, "at the hip the eye carries the whole lean");
    Check(NearVec(f.camera, lean), "and the camera carries all of it");
    Check(sim.rigWrites == 0, "and the rig is never written");
}

void TestSightsUpMovesItAllToTheRig() {
    Sim sim;
    const Vec3 lean = PitchedLean(-0.25f, 0.0f, 0.0f);
    RigLeanFrame f;
    for (unsigned long long t = 0; t <= AdsFade::kLowerMs + 100; t += 16) sim.Frame(lean, true, false, t, true, &f);
    sim.Frame(lean, true, false, AdsFade::kLowerMs + 200, true, &f);
    Check(NearVec(f.rigRequest, lean), "sights up: the rig carries the whole lean");
    Check(NearVec(f.camera, Vec3()), "and the camera, so the aim hook and the reticle, carry none of it");
}

void TestLeaningInStaysOnTheCamera() {
    Sim sim;
    const Vec3 lean = PitchedLean(0.2f, 0.0f, -0.15f);
    RigLeanFrame f;
    for (unsigned long long t = 0; t <= AdsFade::kLowerMs + 100; t += 16) sim.Frame(lean, true, false, t, true, &f);
    const Vec3 eye = sim.Frame(lean, true, false, AdsFade::kLowerMs + 200, true, &f);
    Check(NearVec(f.rigRequest, PitchedLean(0.2f, 0.0f, 0.0f)), "sights up: the rig carries only the lean across the aim");
    Check(NearVec(f.camera, PitchedLean(0.0f, 0.0f, -0.15f)), "and the camera keeps the lean along the aim");
    Check(NearVec(eye, sim.body + lean), "and the eye is where the whole lean puts it");
}

void TestEyeIsTheSameForEverySplit() {
    Sim sim;
    const Vec3 lean = PitchedLean(0.22f, -0.07f, 0.15f);
    bool same = true;
    unsigned long long t = 0;
    for (int cycle = 0; cycle < 3; ++cycle) {
        for (int i = 0; i < 12; ++i, t += 16) same = same && NearVec(sim.Frame(lean, true, false, t), sim.body + lean);
        for (int i = 0; i < 20; ++i, t += 16) same = same && NearVec(sim.Frame(lean, false, false, t), sim.body + lean);
    }
    Check(same, "with a pitched view the eye lands in the same place at every point of the hand-over");

    Sim toggled;
    bool steady = true;
    t = 0;
    for (int i = 0; i < 40; ++i, t += 16) steady = steady && NearVec(toggled.Frame(lean, true, i % 7 < 3, t), toggled.body + lean);
    Check(steady, "and when true free look is toggled mid-aim");
}

void TestTrueFreeLookKeepsTheLeanOnTheCamera() {
    Sim sim;
    const Vec3 lean(0.2f, 0.0f, 0.0f);
    RigLeanFrame f;
    for (unsigned long long t = 0; t < 1000; t += 16) sim.Frame(lean, true, true, t, true, &f);
    Check(NearVec(f.camera, lean) && sim.rigWrites == 0, "true free look: the camera keeps the lean and the rig is never written");
}

// Along Sim's aim axis, which is where a lean in goes.
void TestTheForwardStopHoldsOnlyWithTheSightsUp() {
    const float stop = 0.15f;
    Sim hip;
    hip.lean.SetForwardStop(stop);
    const Vec3 in = hip.aimForward * 0.4f;
    Vec3 eye;
    for (unsigned long long t = 0; t < 1000; t += 16) eye = hip.Frame(in, false, false, t);
    Check(NearVec(eye, hip.body + in), "at the hip a lean in is applied in full");

    for (const bool freeLook : {false, true}) {
        Sim aimed;
        aimed.lean.SetForwardStop(stop);
        RigLeanFrame f;
        for (unsigned long long t = 0; t < 1000; t += 16) eye = aimed.Frame(in, true, freeLook, t, true, &f);
        // Not rigWrites: with the sights locked the rig is handed the rounding
        // left when the lean along the aim is taken off a lean that is all along it.
        Check(NearVec(eye, aimed.body + aimed.aimForward * stop) && NearVec(f.rigRequest, Vec3()),
              freeLook ? "and in free look" : "sights up: the eye stops at the forward stop, on the camera");
    }
}

void TestEveryStopPutsTheRigBack() {
    const Vec3 lean(0.2f, 0.0f, 0.0f);
    RigLeanFrame f;

    Sim lowered;
    for (unsigned long long t = 0; t < 1000; t += 16) lowered.Frame(lean, true, false, t);
    for (unsigned long long t = 1000; t < 1000 + AdsFade::kRaiseMs + 100; t += 16) lowered.Frame(lean, false, false, t, true, &f);
    Check(NearVec(f.rigRequest, Vec3()) && NearVec(f.camera, lean), "sights down: the lean returns to the camera and the rig to its origin");

    Sim positionOff;
    for (unsigned long long t = 0; t < 1000; t += 16) positionOff.Frame(lean, true, false, t);
    Check(positionOff.lean.Stop(), "position off: the stop reports that the rig was carrying the lean");
    positionOff.rigRequest = Vec3();
    positionOff.Frame(lean, false, false, 1016, true, &f);
    Check(NearVec(positionOff.rigApplied, Vec3()), "and nothing is written to the rig after it");

    Sim unavailable;
    for (unsigned long long t = 0; t < 1000; t += 16) unavailable.Frame(lean, true, false, t);
    const Vec3 eye = unavailable.Frame(lean, true, false, 1016, false, &f);
    Check(NearVec(f.rigRequest, Vec3()), "a rig that can no longer move is asked for nothing");
    Check(NearVec(eye, unavailable.body), "and the eye goes where the lean eased out on the camera puts it");
}

struct Wall {
    float distance;
};

LeanObstruction WallQuery(void* context, const Vec3& start, const Vec3& direction, float maxDistance) {
    const Wall& wall = *static_cast<const Wall*>(context);
    (void)start;
    (void)maxDistance;
    LeanObstruction hit;
    hit.queried = true;
    hit.blocked = direction.x > 0.0f;
    hit.distance = wall.distance;
    return hit;
}

void TestTheClampHoldsTheSameStandoffOnEitherCarrier() {
    Wall wall{0.25f};
    const Vec3 lean(0.4f, 0.0f, 0.0f);
    const float skin = cameraunlock::camera::LeanClampSettings{}.skin;
    const Vec3 held = Vec3(0.25f - skin, 0.0f, 0.0f);

    Sim hip;
    hip.query = &WallQuery;
    hip.queryContext = &wall;
    Vec3 eye;
    for (unsigned long long t = 0; t < 1000; t += 16) eye = hip.Frame(lean, false, false, t);
    Check(NearVec(eye, hip.body + held), "at the hip the eye stops the margin short of the wall");

    Sim aimed;
    aimed.query = &WallQuery;
    aimed.queryContext = &wall;
    for (unsigned long long t = 0; t < 1000; t += 16) eye = aimed.Frame(lean, true, false, t);
    Check(NearVec(eye, aimed.body + held) && NearVec(aimed.rigRequest, held),
          "with the rig carrying it the sweep starts from the un-leaned eye and stops at the same place");
}

}  // namespace

int RunRigLeanTests() {
    std::cout << "Rig lean tests\n";

    TestHipNeverWritesTheRig();
    TestSightsUpMovesItAllToTheRig();
    TestLeaningInStaysOnTheCamera();
    TestEyeIsTheSameForEverySplit();
    TestTrueFreeLookKeepsTheLeanOnTheCamera();
    TestTheForwardStopHoldsOnlyWithTheSightsUp();
    TestEveryStopPutsTheRigBack();
    TestTheClampHoldsTheSameStandoffOnEitherCarrier();

    if (g_failures == 0) {
        std::cout << "Rig lean tests: all passed\n";
    } else {
        std::cout << "Rig lean tests: " << g_failures << " failure(s)\n";
    }
    return g_failures;
}
