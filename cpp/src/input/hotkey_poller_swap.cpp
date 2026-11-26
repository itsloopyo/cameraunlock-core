// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#include <cameraunlock/input/hotkey_poller_swap.h>

#include <chrono>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

namespace cameraunlock::input {

namespace {

std::int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

HotkeyCallback HotkeyPollerSwap::Gated(HotkeyCallback action) const {
    if (!action) throw std::invalid_argument("HotkeyPollerSwap::Gated needs an action");
    return [quiet = quiet_until_ms_, action = std::move(action)] {
        if (NowMs() < quiet->load()) return;
        action();
    };
}

void HotkeyPollerSwap::Start(std::unique_ptr<HotkeyPoller> first) {
    if (!first) throw std::invalid_argument("HotkeyPollerSwap::Start needs a poller");
    if (current_) throw std::logic_error("HotkeyPollerSwap::Start is called once; Swap replaces the poller");
    first->Start(kPollIntervalMs);
    current_ = std::move(first);
}

void HotkeyPollerSwap::Swap(std::unique_ptr<HotkeyPoller> next) {
    if (!next) throw std::invalid_argument("HotkeyPollerSwap::Swap needs a poller");
    if (!current_) throw std::logic_error("HotkeyPollerSwap::Swap before Start");
    current_->Stop();
    quiet_until_ms_->store(NowMs() + kQuietMs);
    try {
        next->Start(kPollIntervalMs);
    } catch (const std::exception& e) {
        current_->Start(kPollIntervalMs);
        throw std::runtime_error(std::string("the new keys were not taken, the old ones stay: their polling thread did not start (") +
                                 e.what() + ")");
    }
    next->DisarmPresses();
    current_ = std::move(next);
}

}  // namespace cameraunlock::input
