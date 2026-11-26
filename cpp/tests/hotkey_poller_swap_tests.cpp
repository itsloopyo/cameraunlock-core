// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
// HotkeyPollerSwap as a mod drives it when an edit changes a key list: start one poller, swap in
// another, and every gated action stays quiet for the window after the swap.

#include <cameraunlock/input/hotkey_poller_swap.h>

#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

using cameraunlock::input::HotkeyCallback;
using cameraunlock::input::HotkeyPoller;
using cameraunlock::input::HotkeyPollerSwap;

int g_failures = 0;

void Check(bool cond, const std::string& name) {
    if (cond) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

template <class E>
bool Throws(const std::function<void()>& body) {
    try {
        body();
    } catch (const E&) {
        return true;
    }
    return false;
}

void Refusals() {
    HotkeyPollerSwap swap;
    Check(Throws<std::invalid_argument>([&] { swap.Gated(nullptr); }), "Gated with no action throws std::invalid_argument");
    Check(Throws<std::invalid_argument>([&] { swap.Start(nullptr); }), "Start with no poller throws std::invalid_argument");
    Check(Throws<std::logic_error>([&] { swap.Swap(std::make_unique<HotkeyPoller>()); }),
          "Swap before Start throws std::logic_error");
    Check(swap.Current() == nullptr, "no poller before Start");
    swap.Start(std::make_unique<HotkeyPoller>());
    Check(Throws<std::invalid_argument>([&] { swap.Swap(nullptr); }), "Swap with no poller throws std::invalid_argument");
    Check(Throws<std::logic_error>([&] { swap.Start(std::make_unique<HotkeyPoller>()); }),
          "a second Start throws std::logic_error");
}

void QuietAfterSwap() {
    HotkeyCallback gated;
    int runs = 0;
    {
        HotkeyPollerSwap swap;
        gated = swap.Gated([&runs] { ++runs; });
        gated();
        Check(runs == 1, "a gated action runs before any swap");

        auto first = std::make_unique<HotkeyPoller>();
        HotkeyPoller* first_at = first.get();
        swap.Start(std::move(first));
        Check(swap.Current() == first_at && first_at->IsRunning(), "Start polls the poller handed in");

        auto next = std::make_unique<HotkeyPoller>();
        HotkeyPoller* next_at = next.get();
        swap.Swap(std::move(next));
        Check(swap.Current() == next_at && next_at->IsRunning(), "Swap polls the new poller");
        gated();
        Check(runs == 1, "a gated action does nothing straight after a swap");
        std::this_thread::sleep_for(std::chrono::milliseconds(HotkeyPollerSwap::kQuietMs + 40));
        gated();
        Check(runs == 2, "and runs again once the quiet window has passed");
    }
    gated();
    Check(runs == 3, "a wrapper outlives the swap that made it");
}

}  // namespace

int RunHotkeyPollerSwapTests() {
    std::cout << "\nHotkeyPollerSwap:\n";
    g_failures = 0;
    Refusals();
    QuietAfterSwap();
    return g_failures;
}
