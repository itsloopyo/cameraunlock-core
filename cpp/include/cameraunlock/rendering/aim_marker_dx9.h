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
// Two things differ from the D3D11 and D3D12 bindings. Both are forced by how
// D3D9 is reached, and neither is a choice a consuming mod gets to make:
//
//  - **Ensure() has to be called at load, not from the frame that wants the
//    marker.** The D3D9 overlay reaches Present by hooking
//    IDirect3D9::CreateDevice and reading the vtable off the device the game
//    creates, so the hook must be armed before that call. A first Ensure() at
//    the moment a player selects the marker mode is minutes late: CreateDevice
//    has already returned and is not called again. Call it once during
//    initialisation and let the mode decide only what Publish() says.
//  - **Ready() means the CreateDevice hook is armed, not that anything is being
//    drawn yet.** With the swap chain backends Install() finds a live device;
//    here it can only wait for one. Nothing draws until Publish() is called with
//    `visible`, so the difference costs a mod nothing, but do not read Ready()
//    as "the marker is on screen".
//
// One more thing to know rather than to work around: AimMarkerStyle's colours
// are 0xAABBGGRR, and D3DCOLOR - what DX9DrawContext takes - is 0xAARRGGBB. The
// shipped style is white on black, which reads the same in either order, so the
// default marker is byte-identical to every other backend's. A mod that sets a
// COLOURED style on D3D9 has to give it in D3DCOLOR order.

#ifdef CAMERAUNLOCK_AIM_MARKER_DX9_IMPLEMENTATION
#define CAMERAUNLOCK_AIM_MARKER_IMPLEMENTATION
#endif

#include "cameraunlock/rendering/aim_marker.h"
#include "cameraunlock/rendering/dx9_overlay.h"

namespace cameraunlock::rendering {

struct DX9AimMarkerTraits {
    using Overlay     = DX9Overlay;
    using DrawContext = DX9DrawContext;
    static void SetLogger(OverlayLogFn fn) { SetDX9OverlayLogger(fn); }
};

using AimMarkerDX9 = AimMarker<DX9AimMarkerTraits>;

}  // namespace cameraunlock::rendering
