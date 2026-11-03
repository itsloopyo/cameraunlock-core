#include <windows.h>
#include <cstdio>

#define CAMERAUNLOCK_DX9_OVERLAY_IMPLEMENTATION
#define CAMERAUNLOCK_AIM_MARKER_DX9_IMPLEMENTATION
#include "cameraunlock/rendering/aim_marker_dx9.h"

namespace rndr = cameraunlock::rendering;

namespace {

int checks = 0;
int failures = 0;

void Check(bool ok, const char* what) {
    ++checks;
    if (ok) return;
    ++failures;
    std::fprintf(stderr, "FAIL: %s\n", what);
}

HRESULT __stdcall FakeMethod() { return D3D_OK; }
void* fakeTable[119]{};
struct { void** table; } fakeDevice{fakeTable};

void* seenBefore = nullptr;
int presented = 0;
HRESULT __stdcall OriginalPresent(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*) {
    ++presented;
    return D3D_OK;
}
void BeforePresent(void* device) {
    seenBefore = device;
    if (presented != 0) std::fprintf(stderr, "the before-present callback ran after Present\n");
}

}  // namespace

int main() {
    rndr::OverlayDrawList list(64, 32);
    list.DrawRect(10, 4, 2, 3, 0x80112233u);
    rndr::DX9DrawContext dc(64, 32);
    dc.AppendDrawList(list);
    const auto& v = dc.TriVerts();
    Check(v.size() == 6, "a rect is two triangles");
    Check(v.size() == 6 && v[0].color == 0x80332211u, "0xAABBGGRR becomes D3DCOLOR 0xAARRGGBB");
    Check(v.size() == 6 && v[0].x == 9.5f && v[0].y == 3.5f && v[2].x == 11.5f && v[2].y == 6.5f,
          "corners are shifted half a pixel, as DX9DrawContext's own are");
    Check(v.size() == 6 && v[0].z == 0.0f && v[0].rhw == 1.0f, "vertices are pretransformed at depth 0");

    rndr::OverlayDrawList mark(64, 32);
    rndr::DrawAimMark(mark, 32, 16, rndr::AimMarkerStyle{}, 1.0f);
    rndr::DX9DrawContext markDc(64, 32);
    markDc.AppendDrawList(mark);
    Check(markDc.TriVerts().size() == mark.TriVerts().size() && !mark.TriVerts().empty(),
          "the shared aim mark reaches the D3D9 vertex list whole");

    rndr::AimMarkerDX9 marker;
    marker.Publish(true, 0.0f, 0.0f, 1.0f);
    Check(!marker.Ready(), "the D3D9 marker is not ready before Ensure");

    for (void*& slot : fakeTable) slot = reinterpret_cast<void*>(&FakeMethod);
    auto& s = rndr::detail::State();
    s.origPresent = &OriginalPresent;
    rndr::SetDX9BeforePresentCallback(&BeforePresent);
    auto* device = reinterpret_cast<IDirect3DDevice9*>(&fakeDevice);
    Check(rndr::detail::HookedPresent(device, nullptr, nullptr, nullptr, nullptr) == D3D_OK, "the hooked Present presents");
    Check(seenBefore == device && presented == 1, "the before-present callback sees the presenting device, then the frame is presented");
    s.origPresent = nullptr;

    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
