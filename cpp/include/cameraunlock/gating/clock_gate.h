// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <cstdint>

namespace cameraunlock::gating {

enum class ClockState { Running, Stopped, Warming };

/// Whether the world is being played, from a game clock that the pause menu, the
/// main menu and loading stop. Render frames can outpace clock ticks, so a clock
/// that holds still for a frame or two is not yet stopped; nor is one that a
/// slow frame finds still: in a Headstart rig run (2026-11-24), with the game
/// drawing a few frames a second, the view setup came 150 to 515 ms after the
/// last before the clock had ticked, and each such call held the pose off for
/// over a second. A pause keeps the caller called with the clock still, so the
/// clock is stopped only once a few calls have found it still after
/// kStoppedAfterMs. A level load leaves the clock running for a few seconds
/// under the main menu before it stops, so a clock that jumps, or is seen for
/// the first time, waits out the long warmup; one that picks up where it
/// stopped, as it does leaving the pause menu, waits out the short one.
/// From dead-space-headtracking src/game_state.h:17-46 and
/// src/game_state.cpp:15-43, measured in game there.
class ClockGate {
public:
    static constexpr std::uint64_t kStoppedAfterMs = 150;
    /// Calls that saw the clock still once kStoppedAfterMs had passed since it last moved.
    static constexpr unsigned kStoppedAfterCalls = 3;
    static constexpr std::uint64_t kWarmupMs = 1000;
    static constexpr std::uint64_t kLoadWarmupMs = 6000;
    /// Seconds the clock may move beyond the real time that passed before the move counts as a jump.
    static constexpr float kJumpToleranceSec = 1.0f;

    /// `clock` is the game clock in seconds, `nowMs` a monotonic real time.
    ClockState Update(float clock, std::uint64_t nowMs) {
        if (!seen_) {
            seen_ = true;
            last_ = clock;
            lastAdvanceMs_ = nowMs;
            return ClockState::Stopped;
        }
        if (clock != last_) {
            const float moved = clock - last_;
            const float realSec = static_cast<float>(nowMs - lastAdvanceMs_) / 1000.0f;
            const bool jumped = moved < 0.0f || moved > realSec + kJumpToleranceSec;
            last_ = clock;
            lastAdvanceMs_ = nowMs;
            stillCalls_ = 0;
            if (jumped || !running_) {
                running_ = true;
                runningSinceMs_ = nowMs;
                warmupMs_ = (jumped || loadPending_) ? kLoadWarmupMs : kWarmupMs;
                loadPending_ = false;
            }
        } else if (nowMs - lastAdvanceMs_ >= kStoppedAfterMs && ++stillCalls_ >= kStoppedAfterCalls) {
            running_ = false;
        }
        if (!running_) return ClockState::Stopped;
        if (nowMs - runningSinceMs_ < warmupMs_) return ClockState::Warming;
        return ClockState::Running;
    }

private:
    bool seen_ = false;
    float last_ = 0.0f;
    std::uint64_t lastAdvanceMs_ = 0;
    unsigned stillCalls_ = 0;
    std::uint64_t runningSinceMs_ = 0;
    std::uint64_t warmupMs_ = kLoadWarmupMs;
    bool loadPending_ = true;
    bool running_ = false;
};

}  // namespace cameraunlock::gating
