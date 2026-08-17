// Expands dx12_overlay.h's implementation block so it is typechecked. See README.
#define CAMERAUNLOCK_DX12_OVERLAY_IMPLEMENTATION
#include <windows.h>
#include "cameraunlock/rendering/dx12_overlay.h"

// The DX12 overlay is the only one with cross-thread state (Present,
// ResizeBuffers and Remove all run on different threads), so exercise the
// public shape too - a mutex member that made the class non-movable, or a
// deleted copy the destructor still needs, shows up right here.
namespace {
void InstantiateDX12Overlay() {
    cameraunlock::rendering::DX12Overlay overlay;
    overlay.SetRenderCallback([](float, float) {});
    overlay.SetUpdateCallback([]() {});
    (void)overlay.IsInstalled();
    (void)overlay.IsInitialized();
}
}  // namespace
