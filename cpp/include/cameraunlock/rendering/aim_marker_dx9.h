#pragma once

// The aim marker bound to the Direct3D 9 overlay.
//
// The sibling of aim_marker_dx11.h and aim_marker_dx12.h. The marker itself is
// in aim_marker.h, written once against DrawCross, so a player on a D3D9 game
// gets the same mark, at the same screen position, as one on D3D11 or D3D12.
// This file is the binding and nothing else.
//
// Requires the DX9 overlay, so exactly one translation unit in the consuming mod
// must define CAMERAUNLOCK_DX9_OVERLAY_IMPLEMENTATION and
// CAMERAUNLOCK_AIM_MARKER_DX9_IMPLEMENTATION before including this, and MinHook
// must already be initialised by the time the install runs.
//
// The overlay reaches Present off a probe device's vtable, so Ensure() works at
// any point in the game's life. Only where no probe device can be made does it
// fall back to waiting for the game's own CreateDevice; there Ensure() has to
// come before the game makes its device, and Ready() means that hook is armed,
// not that anything is drawn.
//
// The mark is drawn into the shared draw list (OverlayDrawList, colours
// 0xAABBGGRR as on the other backends), which the overlay turns into D3DCOLOR,
// so a coloured style means the same colour here as on D3D11 and D3D12.

#ifdef CAMERAUNLOCK_AIM_MARKER_DX9_IMPLEMENTATION
#define CAMERAUNLOCK_AIM_MARKER_IMPLEMENTATION
#endif

#include "cameraunlock/rendering/aim_marker.h"
#include "cameraunlock/rendering/dx9_overlay.h"

namespace cameraunlock::rendering {

struct DX9AimMarkerTraits {
    using Overlay     = DX9Overlay;
    using DrawContext = OverlayDrawList;
    static void SetLogger(OverlayLogFn fn) { SetDX9OverlayLogger(fn); }
};

using AimMarkerDX9 = AimMarker<DX9AimMarkerTraits>;

}  // namespace cameraunlock::rendering
