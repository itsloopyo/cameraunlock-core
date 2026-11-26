// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#include <cameraunlock/unreal/ue_screen.h>

#include <stdexcept>

#include <cameraunlock/unreal/ue_params.h>

namespace cameraunlock::unreal {

Screen::Screen(const Reflection& refl, std::uintptr_t processEvent) {
    const std::uintptr_t pe = processEvent;
    viewportSize = ScriptFunction(refl, pe, "/Script/UMG.WidgetLayoutLibrary:GetViewportSize");
    project = ScriptFunction(refl, pe, "/Script/Engine.PlayerController:ProjectWorldLocationToScreen");
    layoutLibrary = refl.Objs().FindPath("/Script/UMG.Default__WidgetLayoutLibrary");
    if (!layoutLibrary) throw std::runtime_error("the WidgetLayoutLibrary default object is not loaded");
    sizeReal = RealOf(viewportSize, "ReturnValue", 2);
    worldReal = RealOf(project, "WorldLocation", 3);
    screenReal = RealOf(project, "ScreenLocation", 2);
    viewportSize.Param("WorldContextObject", sizeof(std::uintptr_t));
    project.Param("bPlayerViewportRelative", sizeof(bool));
    project.Param("ReturnValue", sizeof(bool));
}

std::array<double, 2> Screen::Size(std::uintptr_t controller) const {
    ScriptFunction::Frame f(viewportSize);
    f.Set("WorldContextObject", controller);
    f.Call(layoutLibrary);
    return GetVector2D(f, "ReturnValue", sizeReal);
}

std::optional<std::array<double, 2>> Screen::Project(std::uintptr_t controller, const FVector& world,
                                                     bool playerRelative) const {
    ScriptFunction::Frame f(project);
    SetVector(f, "WorldLocation", worldReal, world);
    f.Set("bPlayerViewportRelative", playerRelative);
    f.Call(controller);
    if (!f.Get<bool>("ReturnValue")) return std::nullopt;
    return GetVector2D(f, "ScreenLocation", screenReal);
}

}  // namespace cameraunlock::unreal
