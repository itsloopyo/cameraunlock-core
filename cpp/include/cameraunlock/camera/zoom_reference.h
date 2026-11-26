// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <cmath>
#include <map>

namespace cameraunlock::camera {

/// The FOV zoom compensation scales against when the game's own setting is not
/// read: the FOV the view has been drawn at for longest. A sprint, a slide or a
/// scope changes the FOV for a while and is outweighed by the time spent at the
/// player's setting, where the widest FOV seen would stay on a sprint's
/// (the-vanishing-of-ethan-carter-redux-headtracking src/fov_override.cpp:107-151). Old time fades with a half-life, so a changed
/// FOV setting is taken up within a few minutes of play. Not thread safe.
class ZoomReference {
public:
    static constexpr double kHalfLifeSeconds = 120.0;
    static constexpr double kBinDeg = 0.25;

    /// Counts `seconds` drawn at `fovDeg`. A FOV or a time that is not positive
    /// counts for nothing.
    void Add(double fovDeg, double seconds) {
        if (!(fovDeg > 0) || !(seconds > 0)) return;
        const double keep = std::exp2(-seconds / kHalfLifeSeconds);
        for (auto it = time_.begin(); it != time_.end();) {
            it->second *= keep;
            if (it->second < 1e-3) it = time_.erase(it);
            else ++it;
        }
        time_[static_cast<long>(std::lround(fovDeg / kBinDeg))] += seconds;
    }

    /// The FOV drawn at for longest, to the nearest kBinDeg; 0 before anything was counted.
    double Base() const {
        long best = 0;
        double most = 0;
        for (const auto& [bin, t] : time_)
            if (t > most) most = t, best = bin;
        return static_cast<double>(best) * kBinDeg;
    }

private:
    std::map<long, double> time_;
};

}  // namespace cameraunlock::camera
