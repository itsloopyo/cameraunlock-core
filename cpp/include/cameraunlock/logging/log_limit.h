// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <atomic>

namespace cameraunlock::logging {

/// A log line that play writes again and again (a periodic report, a state that
/// flips with each menu or each raise of the sights, a value that moves with the
/// head), kept to its first few: the log is flushed to disk per line. Any thread.
class LogLimit {
public:
    explicit constexpr LogLimit(unsigned cap) : cap_(cap) {}

    /// What to end the next line with: "" for one under the cap, a note that no
    /// more are logged for the last, nullptr past it (write nothing).
    const char* Take() {
        if (Spent()) return nullptr;
        const unsigned n = taken_.fetch_add(1, std::memory_order_relaxed);
        if (n >= cap_) return nullptr;
        return n + 1 == cap_ ? "; no more lines like this are logged" : "";
    }

    bool Spent() const { return taken_.load(std::memory_order_relaxed) >= cap_; }

private:
    const unsigned cap_;
    std::atomic<unsigned> taken_{0};
};

}  // namespace cameraunlock::logging
