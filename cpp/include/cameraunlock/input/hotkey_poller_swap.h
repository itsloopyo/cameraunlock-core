// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <cameraunlock/input/hotkey_poller.h>

#include <atomic>
#include <cstdint>
#include <memory>

namespace cameraunlock::input {

/// One running HotkeyPoller, swapped whole for another, as a mod does when an edit to its config
/// changes a key list: build a poller with the new lists, hand it to Swap, and the old one stops.
///
/// Swap stops the old poller before the new one starts, so a press of a key both lists hold
/// never fires twice. A poller's Start forgets which keys were down, so a key held as the swap
/// lands would fire at the new poller's first poll. The caller wraps every action it registers on
/// a poller it hands here in Gated, which does nothing for kQuietMs after a swap (past the new
/// poller's first two polls), and Swap then disarms the new poller's presses, so a key held through
/// the swap does nothing until it is pressed again.
///
/// Not thread-safe: Start, Swap and Current are called from one thread at a time. Gated's wrappers
/// may run on any thread. Swap is never called from an action registered on the poller polling
/// now: it stops that poller, which joins the thread the action runs on.
class HotkeyPollerSwap {
public:
    static constexpr int kPollIntervalMs = 16;
    static constexpr std::int64_t kQuietMs = 40;

    HotkeyPollerSwap() = default;
    HotkeyPollerSwap(const HotkeyPollerSwap&) = delete;
    HotkeyPollerSwap& operator=(const HotkeyPollerSwap&) = delete;
    /// Stops the poller polling now, joining its thread. A mod that keeps one for the life of the
    /// process and wants no join at exit never destroys it.
    ~HotkeyPollerSwap() = default;

    /// `action`, doing nothing in the quiet window after a swap. Wrap every action registered on a
    /// poller handed to Start or Swap. The wrapper may outlive this object. Throws
    /// std::invalid_argument for an empty action.
    HotkeyCallback Gated(HotkeyCallback action) const;

    /// Starts polling `first` every kPollIntervalMs. Throws std::invalid_argument for a null
    /// poller, std::logic_error once started, and what HotkeyPoller::Start throws when its thread
    /// does not start, with nothing started.
    void Start(std::unique_ptr<HotkeyPoller> first);

    /// Polls `next` in place of the poller polling now, and frees that one. Throws
    /// std::invalid_argument for a null poller and std::logic_error before Start. When `next`'s
    /// thread does not start, the old poller is started again with its presses disarmed, `next` is
    /// freed, and std::runtime_error says the old keys stay; when the old one's thread does not start again
    /// either, that error is thrown instead and no poller runs.
    void Swap(std::unique_ptr<HotkeyPoller> next);

    /// The poller polling now, null before Start.
    HotkeyPoller* Current() const { return current_.get(); }

private:
    // Until this steady-clock time, in milliseconds, no gated action runs. Shared with every
    // wrapper Gated made, so one that outlives this object reads memory it still owns.
    std::shared_ptr<std::atomic<std::int64_t>> quiet_until_ms_ = std::make_shared<std::atomic<std::int64_t>>(0);
    std::unique_ptr<HotkeyPoller> current_;
};

}  // namespace cameraunlock::input
