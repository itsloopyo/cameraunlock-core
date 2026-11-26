// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

// Telling the crosshairs of split screen players apart: each crosshair widget
// belongs to the player whose controller owns it, and is held inside that
// player's part of the viewport.
namespace cameraunlock::unreal::split {

// The player whose head moves a crosshair owned by `owner`: the one whose
// controller it is, and player one for a crosshair no player's controller owns
// (none, or one whose player has not drawn a view lately), as every crosshair
// was before players were told apart.
template <std::size_t N>
int OwningPlayer(std::uintptr_t owner, const std::array<std::uintptr_t, N>& controllers) {
    for (std::size_t i = 0; owner && i < N; ++i)
        if (controllers[i] == owner) return static_cast<int>(i);
    return 0;
}

// Whether a player after the first has drawn a view within `withinMs` of
// `nowMs`: split screen. `seenAtMs` is when each player last drew one, 0 for never.
template <std::size_t N>
bool SplitScreen(const std::array<double, N>& seenAtMs, double nowMs, double withinMs) {
    for (std::size_t i = 1; i < N; ++i)
        if (seenAtMs[i] > 0 && nowMs - seenAtMs[i] <= withinMs) return true;
    return false;
}

// A part of the viewport, in viewport pixels.
struct Rect {
    double left = 0, top = 0, right = 0, bottom = 0;
};

// The part of a `size` viewport a player's view is drawn in. `absolute` and
// `relative` are where the drawn view's axis lands, in viewport pixels and in
// pixels of the player's own part, so their difference is that part's corner.
// The axis lands at the middle of the part for a game whose projection is not
// off centre, which gives its far corner. With one player on the screen the
// part is the whole viewport, as it was before players were told apart.
inline Rect PlayerRect(const std::array<double, 2>& absolute, const std::array<double, 2>& relative,
                       const std::array<double, 2>& size, bool split) {
    if (!split) return {0, 0, size[0], size[1]};
    const double left = absolute[0] - relative[0], top = absolute[1] - relative[1];
    return {(std::max)(left, 0.0), (std::max)(top, 0.0), (std::min)(left + 2 * relative[0], size[0]),
            (std::min)(top + 2 * relative[1], size[1])};
}

// `point` held inside `rect`: an aim the head has turned out of a player's
// view keeps its mark at that view's edge, never in another player's. A rect
// turned inside out holds it at its first corner, as std::clamp may not be
// handed a low bound above the high one.
inline std::array<double, 2> HeldIn(const Rect& rect, const std::array<double, 2>& point) {
    return {std::clamp(point[0], rect.left, (std::max)(rect.left, rect.right)),
            std::clamp(point[1], rect.top, (std::max)(rect.top, rect.bottom))};
}

}  // namespace cameraunlock::unreal::split
