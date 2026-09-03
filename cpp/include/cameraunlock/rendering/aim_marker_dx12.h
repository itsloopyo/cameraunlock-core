#pragma once

// The aim marker bound to the Direct3D 12 overlay.
//
// The sibling of aim_marker_dx11.h. The marker itself is in aim_marker.h,
// written once against the shared draw list, so a player on Direct3D 12 gets the
// same mark, at the same screen position, as one on Direct3D 11.
//
// Requires the DX12 overlay, so exactly one translation unit in the consuming
// mod must define CAMERAUNLOCK_DX12_OVERLAY_IMPLEMENTATION and
// CAMERAUNLOCK_AIM_MARKER_DX12_IMPLEMENTATION before including this, and MinHook
// must already be initialised by the time the install runs.
//
// A mod that supports both renderers picks ONE at runtime and installs only that
// one: both backends hook the same DXGI Present, so the second MH_CreateHook on
// that address fails and the second overlay reports an install failure. Decide
// from the process (whether d3d12.dll is loaded), not by trying both.

#ifdef CAMERAUNLOCK_AIM_MARKER_DX12_IMPLEMENTATION
#define CAMERAUNLOCK_AIM_MARKER_IMPLEMENTATION
#endif

#include "cameraunlock/rendering/aim_marker.h"
#include "cameraunlock/rendering/dx12_overlay.h"

namespace cameraunlock::rendering {

struct DX12AimMarkerTraits {
    using Overlay     = DX12Overlay;
    using DrawContext = DX12DrawContext;
    static void SetLogger(OverlayLogFn fn) { SetDX12OverlayLogger(fn); }
};

using AimMarkerDX12 = AimMarker<DX12AimMarkerTraits>;

}  // namespace cameraunlock::rendering
