// Expands dx12_overlay.h's implementation block so it is typechecked. See README.
#define CAMERAUNLOCK_DX12_OVERLAY_IMPLEMENTATION
#include <windows.h>
#include "cameraunlock/rendering/dx12_overlay.h"

namespace {
void InstantiateDX12Overlay() {
    cameraunlock::rendering::DX12Overlay overlay;
    overlay.SetRenderCallback([](cameraunlock::rendering::DX12DrawContext& dc) {
        dc.DrawCross(dc.Width() / 2, dc.Height() / 2, 9.0f, 0xE6FFFFFFu, 2.0f, 3.0f);
    });
    (void)overlay.IsInstalled();
}
}  // namespace
