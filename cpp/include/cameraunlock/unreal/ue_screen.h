// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include <cameraunlock/unreal/ue_math.h>

#include <cameraunlock/unreal/ue_call.h>
#include <cameraunlock/unreal/ue_reflection.h>

namespace cameraunlock::unreal {

// The viewport's size and where a world point lands in it, both asked of the
// engine: WidgetLayoutLibrary:GetViewportSize and
// PlayerController:ProjectWorldLocationToScreen. The engine projects through
// the view it is drawing, so a consumer that hands the engine the head-tracked
// view within the frame gets that view's projection, with the game's own FOV
// axis, aspect and projection rules. A crosshair mover that once worked
// that out itself, got MaintainYFOV wrong and put Deep Rock's crosshair 1.78
// times too far out.
struct Screen {
    Screen() = default;
    // Throws when either function or the layout library's default object is not loaded.
    // `processEvent` is UObject::ProcessEvent (FindProcessEvent).
    Screen(const Reflection& refl, std::uintptr_t processEvent);

    // The viewport's width and height in pixels. Game thread.
    std::array<double, 2> Size(std::uintptr_t controller) const;
    // Where `world` lands in the view being drawn, in viewport pixels, or with
    // `playerRelative` in pixels of the controller's own part of a split
    // screen; nothing when the engine says it does not. Game thread.
    std::optional<std::array<double, 2>> Project(std::uintptr_t controller, const FVector& world,
                                                 bool playerRelative = false) const;

    ScriptFunction viewportSize;
    ScriptFunction project;
    std::uintptr_t layoutLibrary = 0;
    // The size of one component of the viewport's size, of a world location and of a screen location.
    std::size_t sizeReal = 4;
    std::size_t worldReal = 4;
    std::size_t screenReal = 4;
};

}  // namespace cameraunlock::unreal
