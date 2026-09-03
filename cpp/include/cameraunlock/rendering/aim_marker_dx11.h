#pragma once

// The aim marker bound to the Direct3D 11 overlay.
//
// The marker itself is in aim_marker.h, written once against the shared draw
// list so the D3D11 and D3D12 backends put the same mark in the same place. This
// file is the binding and nothing else.
//
// Requires the DX11 overlay, so exactly one translation unit in the consuming
// mod must define CAMERAUNLOCK_DX11_OVERLAY_IMPLEMENTATION and
// CAMERAUNLOCK_AIM_MARKER_DX11_IMPLEMENTATION before including this, and MinHook
// must already be initialised by the time the install runs.
//
// Usage, once per rendered frame, from wherever the mod already knows the aim:
//
//     bool visible = false;
//     if (aiming && mode == AdsMode::Marker && marker.Ensure()) visible = !offScreen;
//     marker.Publish(visible, ndcX, ndcY);

#ifdef CAMERAUNLOCK_AIM_MARKER_DX11_IMPLEMENTATION
#define CAMERAUNLOCK_AIM_MARKER_IMPLEMENTATION
#endif

#include "cameraunlock/rendering/aim_marker.h"
#include "cameraunlock/rendering/dx11_overlay.h"

namespace cameraunlock::rendering {

struct DX11AimMarkerTraits {
    using Overlay     = DX11Overlay;
    using DrawContext = DX11DrawContext;
    static void SetLogger(OverlayLogFn fn) { SetDX11OverlayLogger(fn); }
};

using AimMarkerDX11 = AimMarker<DX11AimMarkerTraits>;

}  // namespace cameraunlock::rendering
