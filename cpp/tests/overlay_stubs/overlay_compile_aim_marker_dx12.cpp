// Expands aim_marker_dx12.h's implementation block, and the overlay's underneath
// it, so both are typechecked. See README.
#define CAMERAUNLOCK_DX12_OVERLAY_IMPLEMENTATION
#define CAMERAUNLOCK_AIM_MARKER_DX12_IMPLEMENTATION
#include <windows.h>
#include "cameraunlock/rendering/aim_marker_dx12.h"

namespace {
void InstantiateDX12Marker() {
    cameraunlock::rendering::AimMarkerDX12 marker;
    marker.SetLogger([](const char*) {});
    marker.SetStyle(cameraunlock::rendering::AimMarkerStyle{});
    (void)marker.Ensure();
    (void)marker.Ready();
    marker.Publish(true, 0.0f, 0.0f);
}
}  // namespace
