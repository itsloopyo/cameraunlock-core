// Tests for the two-parameter smoothing model on the C++ side: the selection
// function, both CalculateSmoothingFactor overloads with the snap removed, the
// default constants, PositionSettings' smoothing fields, the asymmetric Z box
// clamp, and the loopback classifier.
//
// None of this had any C++ coverage before. The suite's 188 passing assertions
// were all pre-existing and none of them touched the migration.

#include <cameraunlock/data/position_settings.h>
#include <cameraunlock/math/smoothing_utils.h>
#include <cameraunlock/processing/position_processor.h>
#include <cameraunlock/protocol/socket_types.h>

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

bool NearEqual(double a, double b, double eps = 1e-6) {
    return std::fabs(a - b) <= eps;
}

void TestDefaultConstants() {
    std::cout << "Default smoothing constants:\n";

    // These are the numbers the C# side pins in SmoothingUtils and that every mod
    // is wired to. A drift between the two languages is invisible at compile time.
    Check(NearEqual(cameraunlock::math::kDefaultLocalSmoothing, 0.0),
          "kDefaultLocalSmoothing is 0.0");
    Check(NearEqual(cameraunlock::math::kDefaultRemoteSmoothing, 0.15),
          "kDefaultRemoteSmoothing is 0.15");
    Check(NearEqual(cameraunlock::math::kFrameInterpolationSpeed, 50.0),
          "kFrameInterpolationSpeed is 50.0");
    Check(NearEqual(cameraunlock::math::kMaxSmoothingSpeed, 0.1),
          "kMaxSmoothingSpeed is 0.1");
}

void TestGetEffectiveSmoothing() {
    std::cout << "GetEffectiveSmoothing (3-arg form):\n";

    using cameraunlock::math::GetEffectiveSmoothing;

    Check(NearEqual(GetEffectiveSmoothing(0.25, 0.75, false), 0.25),
          "a local connection selects the local value");
    Check(NearEqual(GetEffectiveSmoothing(0.25, 0.75, true), 0.75),
          "a remote connection selects the remote value");

    // The whole point of the migration: no floor, no clamp, no baseline. Whatever
    // the user configured is what comes back, including 0.
    Check(NearEqual(GetEffectiveSmoothing(0.0, 0.15, false), 0.0),
          "a configured local 0.0 is returned verbatim, not floored");
    Check(NearEqual(GetEffectiveSmoothing(0.0, 0.0, true), 0.0),
          "a configured remote 0.0 is returned verbatim, not floored");
    Check(NearEqual(GetEffectiveSmoothing(1.0, 0.5, false), 1.0),
          "the maximum passes through unmodified");
}

void TestCalculateSmoothingFactorDouble() {
    std::cout << "CalculateSmoothingFactor (double):\n";

    using cameraunlock::math::CalculateSmoothingFactor;

    const double dt = 1.0 / 60.0;

    // No snap branch: at smoothing 0 the factor must stay strictly inside (0, 1)
    // so frame interpolation keeps running. A snap here would hand every local
    // user raw stepped output, which is exactly what this migration removed.
    double atZero = CalculateSmoothingFactor(0.0, dt);
    Check(atZero > 0.0 && atZero < 1.0, "smoothing 0 yields a factor strictly inside (0, 1)");
    Check(NearEqual(atZero, 1.0 - std::exp(-50.0 * dt), 1e-9),
          "smoothing 0 uses the full frame interpolation speed");

    double atOne = CalculateSmoothingFactor(1.0, dt);
    Check(atOne > 0.0 && atOne < 1.0, "smoothing 1 yields a factor strictly inside (0, 1)");
    Check(atOne < atZero, "heavier smoothing produces a smaller per-frame factor");

    Check(CalculateSmoothingFactor(0.15, dt) < atZero,
          "the remote default smooths harder than the local default");

    // Out-of-range inputs are clamped by the speed bounds rather than escaping.
    double below = CalculateSmoothingFactor(-1.0, dt);
    double above = CalculateSmoothingFactor(2.0, dt);
    Check(NearEqual(below, atZero, 1e-9), "smoothing below 0 clamps to the fastest speed");
    Check(NearEqual(above, atOne, 1e-9), "smoothing above 1 clamps to the slowest speed");
}

void TestCalculateSmoothingFactorFloat() {
    std::cout << "CalculateSmoothingFactor (float):\n";

    using cameraunlock::math::CalculateSmoothingFactor;

    const float dt = 1.0f / 60.0f;

    float atZero = CalculateSmoothingFactor(0.0f, dt);
    Check(atZero > 0.0f && atZero < 1.0f, "smoothing 0 yields a factor strictly inside (0, 1)");

    float atOne = CalculateSmoothingFactor(1.0f, dt);
    Check(atOne > 0.0f && atOne < 1.0f, "smoothing 1 yields a factor strictly inside (0, 1)");
    Check(atOne < atZero, "heavier smoothing produces a smaller per-frame factor");

    // The two overloads must agree, or a mod gets different behaviour depending on
    // which literal type it happened to pass.
    double asDouble = CalculateSmoothingFactor(0.15, static_cast<double>(dt));
    float asFloat = CalculateSmoothingFactor(0.15f, dt);
    Check(NearEqual(static_cast<double>(asFloat), asDouble, 1e-5),
          "the float and double overloads agree");
}

void TestSmoothConvergence() {
    std::cout << "Smooth() convergence:\n";

    const double dt = 1.0 / 60.0;

    // Zero smoothing must converge, just not in a single frame.
    double value = 0.0;
    double afterOne = cameraunlock::math::Smooth(value, 30.0, 0.0, dt);
    Check(afterOne > 0.0 && afterOne < 30.0,
          "one frame at smoothing 0 moves toward the target without reaching it");

    for (int i = 0; i < 60; i++) {
        value = cameraunlock::math::Smooth(value, 30.0, 0.0, dt);
    }
    Check(NearEqual(value, 30.0, 0.05), "smoothing 0 converges within a second");

    double heavy = 0.0;
    for (int i = 0; i < 60; i++) {
        heavy = cameraunlock::math::Smooth(heavy, 30.0, 0.95, dt);
    }
    Check(heavy < value, "heavy smoothing is still lagging after the same second");
}

void TestPositionSettingsSmoothingFields() {
    std::cout << "PositionSettings smoothing fields:\n";

    cameraunlock::PositionSettings defaults;
    Check(NearEqual(defaults.local_smoothing, 0.0f),
          "default-constructed local_smoothing is 0.0");
    Check(NearEqual(defaults.remote_smoothing, 0.15f),
          "default-constructed remote_smoothing is 0.15");

    cameraunlock::PositionSettings factory = cameraunlock::PositionSettings::Default();
    Check(NearEqual(factory.local_smoothing, 0.0f), "Default() local_smoothing is 0.0");
    Check(NearEqual(factory.remote_smoothing, 0.15f), "Default() remote_smoothing is 0.15");
    Check(NearEqual(factory.limit_z, 0.40f), "Default() limit_z is 0.40 (forward)");
    Check(NearEqual(factory.limit_z_back, 0.10f), "Default() limit_z_back is 0.10 (backward)");

    // Distinct sentinel per slot so a one-argument shift shows up as a failure
    // rather than as a plausible-looking number.
    cameraunlock::PositionSettings s(1.01f, 1.02f, 1.03f,
                                     2.01f, 2.02f, 2.03f, 2.04f, 2.05f,
                                     3.01f, 3.02f,
                                     true, false, true);
    Check(NearEqual(s.sensitivity_x, 1.01f) && NearEqual(s.sensitivity_y, 1.02f) &&
              NearEqual(s.sensitivity_z, 1.03f),
          "sensitivity slots map to their own fields");
    Check(NearEqual(s.limit_x, 2.01f) && NearEqual(s.limit_y, 2.02f) &&
              NearEqual(s.limit_y_down, 2.03f) &&
              NearEqual(s.limit_z, 2.04f) && NearEqual(s.limit_z_back, 2.05f),
          "limit slots map to their own fields");
    Check(NearEqual(s.local_smoothing, 3.01f) && NearEqual(s.remote_smoothing, 3.02f),
          "smoothing slots map to their own fields");
    Check(s.invert_x && !s.invert_y && s.invert_z, "inversion slots map to their own fields");
}

void TestPositionProcessorSmoothingSelection() {
    std::cout << "PositionProcessor smoothing selection:\n";

    const float dt = 1.0f / 60.0f;
    cameraunlock::PositionSettings settings = cameraunlock::PositionSettings::Symmetric(
        1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f,
        0.0f, 0.95f);

    auto step = [&](bool is_remote) {
        cameraunlock::PositionProcessor proc;
        proc.SetSettings(settings);
        proc.SetTrackerPivotForward(0.0f);
        proc.SetIsRemoteConnection(is_remote);
        proc.Process(cameraunlock::PositionData(0.0f, 0.0f, 0.0f, 1), cameraunlock::math::Quat4(), dt);
        return proc.Process(cameraunlock::PositionData(0.5f, 0.0f, 0.0f, 2),
                            cameraunlock::math::Quat4(), dt)
            .x;
    };

    float local = step(false);
    float remote = step(true);

    Check(local > remote, "a local connection reacts faster than a remote one");
    Check(remote > 0.0f, "a remote connection still tracks the target");
    Check(local < 0.5f, "frame interpolation still applies at smoothing 0");
}

void TestZClampAsymmetry() {
    std::cout << "Position Z box clamp asymmetry:\n";

    // NEGATIVE z is the forward lean, so limit_z (forward, generous) is the LOWER
    // bound and limit_z_back (backward, tight) the upper one. The C# suite asserts
    // these same numbers in BoxClamp_AsymmetricZLimits_ForwardIsNegativeAndGetsLimitZ.
    // A transposed pair gives forward lean the tight backward budget, which reads
    // as "6DOF barely works" rather than as a bug.
    const float dt = 1.0f / 60.0f;
    cameraunlock::PositionSettings settings = cameraunlock::PositionSettings::Symmetric(
        1.0f, 1.0f, 1.0f,
        0.30f, 0.20f, 0.40f, 0.10f,
        0.0f, 0.0f);

    cameraunlock::PositionProcessor proc;
    proc.SetSettings(settings);
    proc.SetTrackerPivotForward(0.0f);

    cameraunlock::math::Vec3 forward = proc.Process(
        cameraunlock::PositionData(0.0f, 0.0f, -5.0f, 1), cameraunlock::math::Quat4(), dt);
    Check(NearEqual(forward.z, -0.40f, 1e-5), "forward lean clamps to -limit_z (-0.40)");

    proc.ResetSmoothing();
    cameraunlock::math::Vec3 backward = proc.Process(
        cameraunlock::PositionData(0.0f, 0.0f, 5.0f, 2), cameraunlock::math::Quat4(), dt);
    Check(NearEqual(backward.z, 0.10f, 1e-5), "backward lean clamps to +limit_z_back (0.10)");
}

sockaddr_in MakeAddr(const char* dotted) {
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(dotted);
    return addr;
}

void TestLoopbackClassification() {
    std::cout << "Loopback classification:\n";

    // Loopback is the whole 127.0.0.0/8 block, not just 127.0.0.1. The C# suite
    // asserts this same address set against OpenTrackReceiver.IsRemoteAddress in
    // IsRemoteAddress_TreatsWholeLoopbackBlockAsLocal. The two languages disagreeing
    // means the same sender gets LocalSmoothing in one mod and RemoteSmoothing in
    // another, with nothing to catch it.
    Check(!cameraunlock::IsRemoteAddress(MakeAddr("127.0.0.1")), "127.0.0.1 is local");
    Check(!cameraunlock::IsRemoteAddress(MakeAddr("127.0.0.2")), "127.0.0.2 is local");
    Check(!cameraunlock::IsRemoteAddress(MakeAddr("127.1.2.3")), "127.1.2.3 is local");
    Check(!cameraunlock::IsRemoteAddress(MakeAddr("127.255.255.254")),
          "127.255.255.254 is local");

    Check(cameraunlock::IsRemoteAddress(MakeAddr("192.168.1.50")), "192.168.1.50 is remote");
    Check(cameraunlock::IsRemoteAddress(MakeAddr("10.0.0.7")), "10.0.0.7 is remote");
    Check(cameraunlock::IsRemoteAddress(MakeAddr("8.8.8.8")), "8.8.8.8 is remote");
    Check(cameraunlock::IsRemoteAddress(MakeAddr("128.0.0.1")),
          "128.0.0.1 is remote (just above the block)");
    Check(cameraunlock::IsRemoteAddress(MakeAddr("126.255.255.255")),
          "126.255.255.255 is remote (just below the block)");
}

}  // namespace

int RunSmoothingTests() {
    std::cout << "\nSmoothing model tests:\n";
    g_failures = 0;

    TestDefaultConstants();
    TestGetEffectiveSmoothing();
    TestCalculateSmoothingFactorDouble();
    TestCalculateSmoothingFactorFloat();
    TestSmoothConvergence();
    TestPositionSettingsSmoothingFields();
    TestPositionProcessorSmoothingSelection();
    TestZClampAsymmetry();
    TestLoopbackClassification();

    if (g_failures == 0) {
        std::cout << "Smoothing tests: all passed\n";
    } else {
        std::cout << "Smoothing tests: " << g_failures << " failure(s)\n";
    }
    return g_failures;
}
