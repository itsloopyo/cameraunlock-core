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

struct FakeComObject { void** table; };
void* deviceTable[15]{};
FakeComObject fakeDevice{deviceTable};
void* chainTable[9]{};
FakeComObject fakeChain{chainTable};
void* otherChainTable[9]{};
FakeComObject otherChain{otherChainTable};
int refs = 0;
ULONG __stdcall AddRef(IUnknown*) { return ++refs; }
ULONG __stdcall Release(IUnknown*) { return --refs; }
HRESULT __stdcall ChainDevice(IDirect3DSwapChain9*, IDirect3DDevice9** device) {
    ++refs;
    *device = reinterpret_cast<IDirect3DDevice9*>(&fakeDevice);
    return D3D_OK;
}
HRESULT __stdcall ImplicitChain(IDirect3DDevice9*, UINT index, IDirect3DSwapChain9** chain) {
    if (index != 0) return D3DERR_INVALIDCALL;
    ++refs;
    *chain = reinterpret_cast<IDirect3DSwapChain9*>(&fakeChain);
    return D3D_OK;
}

void* seenBefore = nullptr;
int presented = 0;
int chainPresented = 0;
int presentedBefore = -1;
int beforeCalls = 0;
HRESULT chainAnswer = D3D_OK;
HRESULT __stdcall OriginalPresent(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*) {
    ++presented;
    return D3D_OK;
}
HRESULT __stdcall OriginalChainPresent(IDirect3DSwapChain9*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD) {
    ++chainPresented;
    return chainAnswer;
}
// A device Present whose implementation goes on through its swap chain's hooked Present.
HRESULT __stdcall DevicePresentThroughChain(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*) {
    ++presented;
    return rndr::detail9::HookedSwapChainPresent(reinterpret_cast<IDirect3DSwapChain9*>(&fakeChain), nullptr, nullptr,
                                                 nullptr, nullptr, 0);
}
// A swap chain Present whose implementation goes on through the device's hooked Present.
HRESULT __stdcall ChainPresentThroughDevice(IDirect3DSwapChain9*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD) {
    ++chainPresented;
    return rndr::detail9::HookedPresent(reinterpret_cast<IDirect3DDevice9*>(&fakeDevice), nullptr, nullptr, nullptr,
                                        nullptr);
}
void BeforePresent(void* device) {
    seenBefore = device;
    presentedBefore = presented + chainPresented;
    ++beforeCalls;
}

void Reset() {
    seenBefore = nullptr;
    presented = 0;
    chainPresented = 0;
    presentedBefore = -1;
    beforeCalls = 0;
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

    auto& s = rndr::detail9::State();
    s.origPresent = &OriginalPresent;
    rndr::SetDX9BeforePresentCallback(&BeforePresent);
    auto* device = reinterpret_cast<IDirect3DDevice9*>(&fakeDevice);
    Check(rndr::detail9::HookedPresent(device, nullptr, nullptr, nullptr, nullptr) == D3D_OK, "the hooked Present presents");
    Check(seenBefore == device, "the before-present callback sees the presenting device");
    Check(presentedBefore == 0 && presented == 1, "the before-present callback runs before the frame is presented");

    void** tables[] = {deviceTable, chainTable, otherChainTable};
    for (void** table : tables) {
        table[1] = reinterpret_cast<void*>(&AddRef);
        table[2] = reinterpret_cast<void*>(&Release);
    }
    deviceTable[14] = reinterpret_cast<void*>(&ImplicitChain);
    chainTable[8] = reinterpret_cast<void*>(&ChainDevice);
    otherChainTable[8] = reinterpret_cast<void*>(&ChainDevice);
    s.origSwapChainPresent = &OriginalChainPresent;
    auto* chain = reinterpret_cast<IDirect3DSwapChain9*>(&fakeChain);

    Reset();
    Check(rndr::detail9::HookedSwapChainPresent(chain, nullptr, nullptr, nullptr, nullptr, 0) == D3D_OK,
          "the hooked swap chain Present presents");
    Check(seenBefore == device, "the before-present callback sees the swap chain's device");
    Check(beforeCalls == 1 && presentedBefore == 0 && chainPresented == 1,
          "the before-present callback runs once, before the swap chain presents");
    Check(refs == 0, "the swap chain's device and the implicit swap chain are released after the frame");

    Reset();
    Check(rndr::detail9::HookedSwapChainPresent(reinterpret_cast<IDirect3DSwapChain9*>(&otherChain), nullptr, nullptr,
                                                nullptr, nullptr, 0) == D3D_OK &&
              chainPresented == 1,
          "an additional swap chain presents");
    Check(beforeCalls == 0, "an additional swap chain's Present draws nothing");
    Check(refs == 0, "an additional swap chain's device is released");

    Reset();
    chainAnswer = D3DERR_WASSTILLDRAWING;
    rndr::detail9::HookedSwapChainPresent(chain, nullptr, nullptr, nullptr, nullptr, D3DPRESENT_DONOTWAIT);
    rndr::detail9::HookedSwapChainPresent(chain, nullptr, nullptr, nullptr, nullptr, D3DPRESENT_DONOTWAIT);
    chainAnswer = D3D_OK;
    rndr::detail9::HookedSwapChainPresent(chain, nullptr, nullptr, nullptr, nullptr, D3DPRESENT_DONOTWAIT);
    Check(chainPresented == 3 && beforeCalls == 1, "a Present retried while the GPU is still drawing draws its frame once");
    Reset();
    rndr::detail9::HookedSwapChainPresent(chain, nullptr, nullptr, nullptr, nullptr, 0);
    Check(beforeCalls == 1, "the frame after a retried Present is drawn");

    Reset();
    s.origPresent = &DevicePresentThroughChain;
    Check(rndr::detail9::HookedPresent(device, nullptr, nullptr, nullptr, nullptr) == D3D_OK,
          "a device Present that goes through the swap chain's presents");
    Check(beforeCalls == 1 && presented == 1 && chainPresented == 1,
          "a device Present that goes through the swap chain's Present draws the frame once");

    Reset();
    s.origPresent = &OriginalPresent;
    s.origSwapChainPresent = &ChainPresentThroughDevice;
    Check(rndr::detail9::HookedSwapChainPresent(chain, nullptr, nullptr, nullptr, nullptr, 0) == D3D_OK,
          "a swap chain Present that goes through the device's presents");
    Check(beforeCalls == 1 && presented == 1 && chainPresented == 1,
          "a swap chain Present that goes through the device's Present draws the frame once");

    Reset();
    s.origSwapChainPresent = &OriginalChainPresent;
    rndr::detail9::HookedPresent(device, nullptr, nullptr, nullptr, nullptr);
    rndr::detail9::HookedSwapChainPresent(chain, nullptr, nullptr, nullptr, nullptr, 0);
    Check(beforeCalls == 2, "Presents one after another each draw their own frame");
    Check(refs == 0, "every reference taken in a Present is released");
    s.origPresent = nullptr;
    s.origSwapChainPresent = nullptr;

    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
