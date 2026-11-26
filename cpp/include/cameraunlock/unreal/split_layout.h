// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <cstdint>
#include <optional>

// Where each local player's view sits in the window when Unreal Engine 4 splits
// the screen, as UGameViewportClient lays it out: two, three and four players
// by GameMapsSettings' TwoPlayerSplitscreenLayout, ThreePlayerSplitscreenLayout
// and FourPlayerSplitscreenLayout. Pure, with no engine behind it.
namespace cameraunlock::unreal::split {

// A part of the window, in fractions of its width and height from its top-left corner.
struct Part {
    double x = 0, y = 0, w = 1, h = 1;
};

// ETwoPlayerSplitScreenType, EThreePlayerSplitScreenType and EFourPlayerSplitScreenType.
enum class Two : std::uint8_t { Horizontal = 0, Vertical = 1 };
enum class Three : std::uint8_t { FavorTop = 0, FavorBottom = 1, Vertical = 2, Horizontal = 3 };
enum class Four : std::uint8_t { Grid = 0, Vertical = 1, Horizontal = 2 };

// Player `index` (from 0) of `count` on a split screen; one player has the
// whole window. Nothing for an index outside the players, more than four
// players or a layout the engine does not have.
inline std::optional<Part> PartOf(int index, int count, Two two, Three three, Four four) {
    if (index < 0 || index >= count) return std::nullopt;
    const double third = 1.0 / 3.0;
    switch (count) {
    case 1: return Part{};
    case 2:
        if (two == Two::Horizontal) return Part{0, 0.5 * index, 1, 0.5};
        if (two == Two::Vertical) return Part{0.5 * index, 0, 0.5, 1};
        return std::nullopt;
    case 3: {
        switch (three) {
        case Three::FavorTop: {
            const Part parts[] = {{0, 0, 1, 0.5}, {0, 0.5, 0.5, 0.5}, {0.5, 0.5, 0.5, 0.5}};
            return parts[index];
        }
        case Three::FavorBottom: {
            const Part parts[] = {{0, 0, 0.5, 0.5}, {0.5, 0, 0.5, 0.5}, {0, 0.5, 1, 0.5}};
            return parts[index];
        }
        case Three::Vertical: return Part{third * index, 0, third, 1};
        case Three::Horizontal: return Part{0, third * index, 1, third};
        }
        return std::nullopt;
    }
    case 4:
        switch (four) {
        case Four::Grid: return Part{0.5 * (index % 2), 0.5 * (index / 2), 0.5, 0.5};
        case Four::Vertical: return Part{0.25 * index, 0, 0.25, 1};
        case Four::Horizontal: return Part{0, 0.25 * index, 1, 0.25};
        }
        return std::nullopt;
    }
    return std::nullopt;
}

// A point in a part's own -1..1 frame (x right, y up) as the same point in the
// whole window's -1..1 frame.
inline void ToWindow(const Part& part, double x, double y, double& windowX, double& windowY) {
    windowX = (part.x + (x + 1) * 0.5 * part.w) * 2 - 1;
    windowY = 1 - (part.y + (1 - y) * 0.5 * part.h) * 2;
}

}  // namespace cameraunlock::unreal::split
