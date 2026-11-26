// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
// The game-clock gate (cameraunlock/gating/clock_gate.h), ported from
// Headstart's native/frostbite/tests/fb_tests.cpp, and the log line limit
// (cameraunlock/logging/log_limit.h), from native/idtech5/tests.

#include <cameraunlock/gating/clock_gate.h>
#include <cameraunlock/logging/log_limit.h>

#include <cstdint>
#include <iostream>
#include <string>

namespace {

using cameraunlock::gating::ClockGate;
using cameraunlock::gating::ClockState;

int g_failures = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

void TestClockGate() {
    std::cout << "ClockGate:\n";
    ClockGate g;
    std::uint64_t now = 1000;
    float clock = 50;
    Check(g.Update(clock, now) == ClockState::Stopped, "a clock seen for the first time is stopped");
    bool warming = true;
    for (int i = 0; i < 10; ++i) {
        now += 100;
        clock += 0.1f;
        warming = warming && g.Update(clock, now) == ClockState::Warming;
    }
    Check(warming, "the first run after load waits out the long warmup");
    now += ClockGate::kLoadWarmupMs;
    clock += ClockGate::kLoadWarmupMs / 1000.0f;
    Check(g.Update(clock, now) == ClockState::Running, "and then runs");

    now += 30;
    Check(g.Update(clock, now) == ClockState::Running, "a frame with the clock still is not a pause");
    now += 5;
    bool slow = g.Update(clock, now) == ClockState::Running;
    now += 5;
    slow = slow && g.Update(clock, now) == ClockState::Running;
    now += 400;
    slow = slow && g.Update(clock, now) == ClockState::Running;
    now += 30;
    clock += 0.43f;
    slow = slow && g.Update(clock, now) == ClockState::Running;
    Check(slow, "nor is one slow frame before the clock moves, even after quick still calls");
    bool quick = true;
    for (int i = 0; i < 10; ++i) {
        now += 10;
        quick = quick && g.Update(clock, now) == ClockState::Running;
    }
    Check(quick, "many still calls inside kStoppedAfterMs are not a pause either");

    bool held = true;
    for (unsigned i = 1; i < ClockGate::kStoppedAfterCalls; ++i) {
        now += ClockGate::kStoppedAfterMs;
        held = held && g.Update(clock, now) == ClockState::Running;
    }
    now += ClockGate::kStoppedAfterMs;
    Check(held && g.Update(clock, now) == ClockState::Stopped, "the clock held still over enough calls and time is stopped");

    now += 5000;
    clock += 0.016f;
    Check(g.Update(clock, now) == ClockState::Warming, "picking up where it stopped waits out the short warmup");
    now += ClockGate::kWarmupMs;
    clock += ClockGate::kWarmupMs / 1000.0f;
    Check(g.Update(clock, now) == ClockState::Running, "and then runs");

    now += 16;
    clock += 600;
    Check(g.Update(clock, now) == ClockState::Warming, "a jump (a save loaded) is a load");
    now += ClockGate::kWarmupMs + 100;
    clock += (ClockGate::kWarmupMs + 100) / 1000.0f;
    Check(g.Update(clock, now) == ClockState::Warming, "and waits out the long warmup");
}

void TestLogLimit() {
    std::cout << "LogLimit:\n";
    cameraunlock::logging::LogLimit limit(2);
    Check(!limit.Spent(), "a fresh limit is not spent");
    const char* first = limit.Take();
    Check(first && std::string(first).empty(), "the first line ends with nothing");
    const char* last = limit.Take();
    Check(last && std::string(last) == "; no more lines like this are logged", "the last says no more are logged");
    Check(limit.Spent() && limit.Take() == nullptr, "past the cap nothing is written");
    cameraunlock::logging::LogLimit none(0);
    Check(none.Spent() && none.Take() == nullptr, "a cap of 0 writes nothing");
}

}  // namespace

int RunClockGateTests() {
    std::cout << "Clock gate and log limit tests\n";
    TestClockGate();
    TestLogLimit();
    if (g_failures == 0) {
        std::cout << "Clock gate and log limit tests: all passed\n";
    } else {
        std::cout << "Clock gate and log limit tests: " << g_failures << " failure(s)\n";
    }
    return g_failures;
}
