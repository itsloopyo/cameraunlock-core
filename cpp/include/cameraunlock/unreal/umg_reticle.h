#pragma once

// Moving the game's OWN crosshair, for UE titles that draw a fixed one.
//
// The reticle doctrine says the shot stays where the mouse put it and the mark
// moves to meet it. Most games hand a mod that for free: they project a world
// aim point to the screen every frame, and a mod that lets that projection see
// the head-tracked view gets its crosshair placed by the engine's own matrices.
//
// Some do not. A game whose aim is always the exact middle of the frame has no
// reason to project anything - the middle is a constant - so it paints its
// crosshair at a fixed spot and is right until head tracking moves the camera
// off the aim. There is then no projection to hand a better view to, and the
// mod owes the player a mark somewhere else.
//
// Two ways to pay that. Draw a mark over the top (rendering/aim_marker_dx11.h),
// which needs a swap chain the overlay can attach to and leaves the game's own
// wrong mark on screen next to the right one. Or MOVE the game's mark, which is
// this file. Prefer this one wherever the crosshair is a UMG widget: one mark
// on screen instead of two, no swap chain touched, and it is graphics-API
// agnostic, which matters because the shared overlay is Direct3D 11 only.
//
// The mechanism is UWidget::SetRenderTranslation(FVector2D), the engine's own
// setter. It writes RenderTransform.Translation and then invalidates the Slate
// widget underneath; writing that field directly moves the property and not the
// pixels, so the setter's address is what a consumer must supply, from its
// per-build offset profile like any other RVA.
//
// Two things the consumer owns, because neither is knowable here:
//   - WHERE the mark goes. Hand over a screen-pixel offset from the widget's
//     laid-out position, derived from the same rotation composition the camera
//     hook applies. This file does no projection and must never grow one.
//   - the DPI scale. Render translation is in the widget's local space and the
//     viewport scales that by the game's DPI curve, so a consumer that wants
//     screen pixels divides by UWidgetLayoutLibrary::GetViewportScale first.
//     SetScale() keeps that in one place.

#include <cstdint>
#include <cstring>

#include <cameraunlock/unreal/ue_runtime.h>

namespace cameraunlock::unreal {

// Find the live instance of a named widget inside a named widget blueprint.
//
// UMG names are only unique within a tree: every HUD in a game can hold an
// Image called "Crosshair", and the class default object holds one too. So the
// match is on the widget's own class and name, plus a class name that must
// appear somewhere up its outer chain, plus a rejection of anything whose name
// begins with the engine's "Default__" - a CDO's widget is a template that is
// never drawn, and moving it changes nothing on screen while looking exactly
// like success.
//
// `widgetClass` and `widgetName` match the whole name case-insensitively;
// `ownerClassContains` is a case-insensitive substring of the class name of some
// ancestor. Returns 0 when nothing matches, which is the honest answer when a
// game patch renames the widget - the caller must leave the crosshair alone
// rather than move something else.
std::uintptr_t FindWidgetInstance(const char* widgetClass, const char* widgetName,
                                  const char* ownerClassContains);

// Moves one UMG widget to a per-frame screen offset.
//
// Not an owner of anything: the widget pointer is a live UObject the caller
// found and must drop when the level it belongs to goes away, and the setter
// address is the caller's per-build RVA.
class UmgReticle {
public:
    // The address of UWidget::SetRenderTranslation in the running module.
    // Returns false for a zero address, which leaves the reticle inert.
    bool Bind(std::uintptr_t setRenderTranslationAddr) {
        setter_ = reinterpret_cast<SetRenderTranslationFn>(setRenderTranslationAddr);
        return setter_ != nullptr;
    }

    void SetWidget(std::uintptr_t widget) { widget_ = widget; }
    std::uintptr_t Widget() const { return widget_; }

    // Viewport DPI scale, so MoveTo() can be handed screen pixels. Defaults to
    // 1.0, which is correct only at the scale the HUD was authored for.
    void SetScale(float viewportScale) {
        scale_ = (viewportScale > 0.0f) ? viewportScale : 1.0f;
    }

    bool Ready() const { return setter_ != nullptr && widget_ != 0; }

    // Offset from where the game laid the widget out, in SCREEN pixels, x right
    // and y down. (0, 0) puts it back exactly where the game wants it, which is
    // what a mod calls when tracking stops - never leave a mark parked at the
    // last offset it had.
    bool MoveTo(float screenDx, float screenDy) {
        if (!Ready()) return false;
        // FVector2D is two floats in eight bytes, which the x64 ABI passes in
        // the integer register rather than in xmm - a struct of exactly that
        // size goes by value as if it were a uint64.
        const float local[2] = { screenDx / scale_, screenDy / scale_ };
        std::uint64_t packed = 0;
        std::memcpy(&packed, local, sizeof(packed));
        setter_(reinterpret_cast<void*>(widget_), packed);
        return true;
    }

private:
    using SetRenderTranslationFn = void(__fastcall*)(void* widget, std::uint64_t translation);

    SetRenderTranslationFn setter_ = nullptr;
    std::uintptr_t widget_ = 0;
    float scale_ = 1.0f;
};

}  // namespace cameraunlock::unreal
