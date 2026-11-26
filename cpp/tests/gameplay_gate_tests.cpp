// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core

// With no REFramework API in the test process, a refresh throws "API not
// initialized" from API::get(), so whether a call refreshed is whether it threw.

#include <cameraunlock/reframework/gameplay_gate.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <iostream>
#include <stdexcept>

namespace {

int g_failures = 0;

void Check(bool cond, const char* name) {
    std::cout << (cond ? "  [PASS] " : "  [FAIL] ") << name << "\n";
    if (!cond) ++g_failures;
}

bool Refreshed(cameraunlock::reframework::GameplayGate& gate) {
    try {
        gate.IsInGameplay();
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

}  // namespace

int main() {
    using cameraunlock::reframework::GameplayGate;
    std::cout << "\n=== Gameplay Gate Tests ===\n";

    GameplayGate throttled(nullptr, nullptr);
    Check(Refreshed(throttled), "the default gate refreshes on its first call");
    const unsigned long long before = GetTickCount64();
    const bool again = Refreshed(throttled);
    // A thread held off the CPU for 100 ms between the two calls is not a failure.
    if (GetTickCount64() - before < GameplayGate::kDefaultCheckIntervalMs)
        Check(!again, "and not again within its 100 ms");

    GameplayGate everyFrame(nullptr, nullptr, 0);
    Check(Refreshed(everyFrame), "a gate with interval 0 refreshes on its first call");
    Check(Refreshed(everyFrame), "and on the next");
    Check(Refreshed(everyFrame), "and every one after");

    GameplayGate explicitDefault(nullptr, nullptr, GameplayGate::kDefaultCheckIntervalMs);
    Check(Refreshed(explicitDefault), "a gate given kDefaultCheckIntervalMs refreshes on its first call");
    const unsigned long long since = GetTickCount64();
    const bool soon = Refreshed(explicitDefault);
    if (GetTickCount64() - since < GameplayGate::kDefaultCheckIntervalMs)
        Check(!soon, "and not again within its 100 ms, as the two-argument gate");
    std::cout << g_failures << " gameplay gate check(s) failed\n";
    return g_failures == 0 ? 0 : 1;
}
