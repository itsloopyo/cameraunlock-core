#pragma once

#include <atomic>

namespace cameraunlock::input {

// Bridge between hotkey callbacks and the thread that owns the mutated state.
//
// HotkeyPoller callbacks fire on its background polling thread, but most mod
// actions touch state owned by the render thread: processor/interpolator
// smoothing state, GUI bookkeeping containers, engine objects. Mutating an
// unordered_set (or similar) from the hotkey thread while the render thread
// reads it is a genuine heap-corruption race - not a benign float race.
//
// The hotkey callback calls Request(); the owning thread calls Consume() at a
// safe point each frame and runs the action when it returns true. Requests
// coalesce: multiple Request() calls before the next Consume() run the action
// once.
class DeferredAction {
public:
    void Request() { m_requested.store(true, std::memory_order_relaxed); }

    bool Consume() { return m_requested.exchange(false, std::memory_order_relaxed); }

private:
    std::atomic<bool> m_requested{false};
};

} // namespace cameraunlock::input
