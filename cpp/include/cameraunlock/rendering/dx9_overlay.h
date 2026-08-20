#pragma once

// DX9 Overlay System
// Minimal-dep DX9 overlay for drawing crosshair-style 2D primitives over a game.
//
// The DX9 analog of dx9_overlay.h's DX11 sibling: fixed-function pipeline,
// pretransformed (D3DFVF_XYZRHW) vertices drawn with DrawPrimitiveUP, so there
// are no shaders, no managed resources, and nothing to recreate on device reset.
//
// Design goals:
//   - No ImGui, no kiero. Consumers already vendor MinHook for game hooks; reuse it.
//   - Header-only with a single TU defining CAMERAUNLOCK_DX9_OVERLAY_IMPLEMENTATION.
//   - Pixel-space drawing API (top-left = 0,0). Colours are D3DCOLOR (0xAARRGGBB).
//
// Required external dependencies (TU with CAMERAUNLOCK_DX9_OVERLAY_IMPLEMENTATION):
//   - <d3d9.h>
//   - <MinHook.h>
//
// Example:
//   DX9Overlay overlay;
//   overlay.SetRenderCallback([](DX9DrawContext& dc) {
//       dc.DrawCross(dc.Width()/2, dc.Height()/2, 12.0f, 0xFFFFFFFF, 1.5f, 4.0f);
//   });
//   overlay.Install();
//   ...
//   overlay.Remove();

#include <cstdint>
#include <functional>
#include <vector>

namespace cameraunlock::rendering {

// D3DCOLOR: 0xAARRGGBB (fixed-function diffuse).
using Argb = uint32_t;

// Pretransformed vertex: screen-space x/y, rhw=1, packed colour.
struct DX9OverlayVertex {
    float x;
    float y;
    float z;
    float rhw;
    Argb  color;
};

// Drawing context passed to the render callback. Accumulates triangles into a
// CPU-side vector; the overlay flushes them once per frame with DrawPrimitiveUP.
class DX9DrawContext {
public:
    DX9DrawContext(float w, float h) : m_width(w), m_height(h) {}

    float Width()  const { return m_width;  }
    float Height() const { return m_height; }

    void DrawLine(float x1, float y1, float x2, float y2, Argb color, float thickness = 1.0f);
    void DrawRect(float x, float y, float w, float h, Argb color);
    void DrawDot(float cx, float cy, float radius, Argb color);

    // Crosshair: 4 line segments centred at (cx, cy), each `arm` long with a
    // central `gap` left empty.
    void DrawCross(float cx, float cy, float arm, Argb color, float thickness = 1.0f, float gap = 0.0f);

    const std::vector<DX9OverlayVertex>& TriVerts() const { return m_triVerts; }

private:
    float m_width;
    float m_height;
    std::vector<DX9OverlayVertex> m_triVerts;  // triangle list
};

using DX9RenderCallback = std::function<void(DX9DrawContext&)>;

// Optional diagnostic log sink.
using DX9LogFn = void (*)(const char* msg);
void SetDX9OverlayLogger(DX9LogFn fn);

// Called once, on the game thread, when the game's real D3D9 device is captured
// (via the CreateDevice hook), passing its IDirect3DDevice9 vtable so a consumer
// can hook additional device methods (e.g. draw-call reticle suppression).
using DX9DeviceReadyFn = void (*)(void** deviceVTable);
void SetDX9DeviceReadyCallback(DX9DeviceReadyFn fn);

class DX9Overlay {
public:
    DX9Overlay() = default;
    ~DX9Overlay();
    DX9Overlay(const DX9Overlay&)            = delete;
    DX9Overlay& operator=(const DX9Overlay&) = delete;

    // Install the Present hook. Caller must have already initialized MinHook
    // (MH_Initialize). Returns false on failure.
    bool Install();

    // Tear down the hook.
    void Remove();

    void SetRenderCallback(DX9RenderCallback cb);

    bool IsInstalled() const { return m_hookInstalled; }

private:
    DX9RenderCallback m_callback;
    bool m_hookInstalled = false;
};

#ifdef CAMERAUNLOCK_DX9_OVERLAY_IMPLEMENTATION

// ============================================================================
// Implementation
// ============================================================================

} // namespace cameraunlock::rendering - re-opened after includes

#include <d3d9.h>
#include <MinHook.h>
#include <Windows.h>
#include <cmath>
#include <memory>
#include <mutex>

#pragma comment(lib, "d3d9.lib")

namespace cameraunlock::rendering {

// ---------- DX9DrawContext ----------------------------------------------------

inline void DX9DrawContext::DrawRect(float x, float y, float w, float h, Argb color) {
    // Pretransformed coords are sampled at pixel centres; the -0.5 texel shift
    // keeps thin primitives from straddling two pixels.
    DX9OverlayVertex v0{x - 0.5f,     y - 0.5f,     0.0f, 1.0f, color};
    DX9OverlayVertex v1{x + w - 0.5f, y - 0.5f,     0.0f, 1.0f, color};
    DX9OverlayVertex v2{x + w - 0.5f, y + h - 0.5f, 0.0f, 1.0f, color};
    DX9OverlayVertex v3{x - 0.5f,     y + h - 0.5f, 0.0f, 1.0f, color};
    m_triVerts.push_back(v0); m_triVerts.push_back(v1); m_triVerts.push_back(v2);
    m_triVerts.push_back(v0); m_triVerts.push_back(v2); m_triVerts.push_back(v3);
}

inline void DX9DrawContext::DrawLine(float x1, float y1, float x2, float y2, Argb color, float thickness) {
    // Render a line as a thin quad so thickness is reliable (line-list raster is
    // 1px only on many adapters).
    float dx = x2 - x1, dy = y2 - y1;
    float len = std::sqrt(dx*dx + dy*dy);
    if (len < 1e-3f) return;
    float nx = -dy / len, ny = dx / len;   // perpendicular
    float t = thickness * 0.5f;
    float ox = nx * t, oy = ny * t;

    DX9OverlayVertex a{x1 - ox - 0.5f, y1 - oy - 0.5f, 0.0f, 1.0f, color};
    DX9OverlayVertex b{x2 - ox - 0.5f, y2 - oy - 0.5f, 0.0f, 1.0f, color};
    DX9OverlayVertex c{x2 + ox - 0.5f, y2 + oy - 0.5f, 0.0f, 1.0f, color};
    DX9OverlayVertex d{x1 + ox - 0.5f, y1 + oy - 0.5f, 0.0f, 1.0f, color};
    m_triVerts.push_back(a); m_triVerts.push_back(b); m_triVerts.push_back(c);
    m_triVerts.push_back(a); m_triVerts.push_back(c); m_triVerts.push_back(d);
}

inline void DX9DrawContext::DrawDot(float cx, float cy, float radius, Argb color) {
    constexpr int kSegments = 16;
    constexpr float kTau = 6.28318530718f;
    DX9OverlayVertex centre{cx - 0.5f, cy - 0.5f, 0.0f, 1.0f, color};
    for (int i = 0; i < kSegments; ++i) {
        float a0 = (kTau * i) / kSegments;
        float a1 = (kTau * (i + 1)) / kSegments;
        DX9OverlayVertex p0{cx + std::cos(a0) * radius - 0.5f, cy + std::sin(a0) * radius - 0.5f, 0.0f, 1.0f, color};
        DX9OverlayVertex p1{cx + std::cos(a1) * radius - 0.5f, cy + std::sin(a1) * radius - 0.5f, 0.0f, 1.0f, color};
        m_triVerts.push_back(centre);
        m_triVerts.push_back(p0);
        m_triVerts.push_back(p1);
    }
}

inline void DX9DrawContext::DrawCross(float cx, float cy, float arm, Argb color, float thickness, float gap) {
    if (arm <= gap) return;
    DrawLine(cx - arm, cy, cx - gap, cy, color, thickness);
    DrawLine(cx + gap, cy, cx + arm, cy, color, thickness);
    DrawLine(cx, cy - arm, cx, cy - gap, color, thickness);
    DrawLine(cx, cy + gap, cx, cy + arm, color, thickness);
}

// ---------- DX9Overlay --------------------------------------------------------
//
// Singleton-by-design: the Present hook is a free function, so we keep one
// active overlay at a time. Every mod we ship has exactly one HUD.

namespace detail {

struct DX9State {
    bool hookInstalled = false;
    void* createDeviceTarget = nullptr;
    HRESULT (__stdcall* origCreateDevice)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD,
                                          D3DPRESENT_PARAMETERS*, IDirect3DDevice9**) = nullptr;

    void* presentTarget = nullptr;
    HRESULT (__stdcall* origPresent)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*) = nullptr;

    void** deviceVTable = nullptr;  // captured real IDirect3DDevice9 vtable
    bool   deviceCaptured = false;
    bool   presentHooked = false;

    // The callback is assigned from the mod thread and invoked from the render
    // thread; a settings hot-reload that re-registers it would otherwise tear
    // the std::function under RenderFrame.
    // Held by shared_ptr so RenderFrame copies a refcount rather than the functor.
    // Copying a std::function allocates whenever its target exceeds the small-object
    // buffer, which any lambda capturing more than a pointer or two does - that was a
    // heap allocation per frame on the render thread, inside a hooked Present.
    // Replacing the pointer keeps an in-flight invocation's target alive, which is one
    // of the two things the copy was doing.
    //
    // The other is a deliberate behaviour change: the per-frame copy also gave each
    // invocation a PRIVATE target, so a mutable lambda's captured state was rebuilt
    // from the master every frame and every mutation thrown away. Invocations now
    // share one target, so a `[n = 0](...) mutable` counter actually advances. That is
    // almost certainly what anyone writing one intended, but it is a change.
    std::shared_ptr<const DX9RenderCallback> callback;
    std::mutex        callbackMutex;
    DX9LogFn          logFn = nullptr;
    DX9DeviceReadyFn  deviceReadyFn = nullptr;
    bool              firstPresentLogged = false;
};

inline DX9State& State() {
    static DX9State s;
    return s;
}

inline void Log(const char* msg) {
    auto& s = State();
    if (s.logFn) s.logFn(msg);
}

inline void RenderFrame(IDirect3DDevice9* dev) {
    auto& s = State();

    std::shared_ptr<const DX9RenderCallback> callback;
    {
        std::lock_guard<std::mutex> lock(s.callbackMutex);
        callback = s.callback;
    }
    if (!callback || !*callback) return;

    D3DVIEWPORT9 vp = {};
    if (FAILED(dev->GetViewport(&vp)) || vp.Width == 0 || vp.Height == 0) return;

    DX9DrawContext dc(static_cast<float>(vp.Width), static_cast<float>(vp.Height));
    (*callback)(dc);
    const auto& verts = dc.TriVerts();
    if (verts.empty()) return;

    // Snapshot the full device state and restore it after our draw, so the
    // game's pipeline is left exactly as it was.
    IDirect3DStateBlock9* block = nullptr;
    if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &block)) || !block) return;

    dev->SetVertexShader(nullptr);
    dev->SetPixelShader(nullptr);
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    dev->SetTexture(0, nullptr);
    dev->SetStreamSource(0, nullptr, 0, 0);
    dev->SetIndices(nullptr);

    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0000000F);

    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);

    UINT triCount = static_cast<UINT>(verts.size() / 3);
    if (triCount > 0) {
        dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, triCount, verts.data(), sizeof(DX9OverlayVertex));
    }

    block->Apply();
    block->Release();
}

inline HRESULT __stdcall HookedPresent(IDirect3DDevice9* dev, const RECT* src, const RECT* dst,
                                       HWND wnd, const RGNDATA* dirty) {
    auto& s = State();
    // Remove() nulls this; a thread that entered just before it ran has no
    // trampoline left to forward to.
    auto orig = s.origPresent;
    // REACHABLE. MH_DisableHook restores the original bytes, but it does not drain
    // threads already inside this detour - it relocates instruction pointers only
    // within the target and trampoline regions, and this function is not in either.
    // A thread that entered before Remove() and reaches here afterwards finds the
    // trampoline nulled, which is the point: the alternative is calling through
    // memory MH_RemoveHook has already returned to MinHook's pool. Returning an
    // error is correct - the frame genuinely was not presented.
    if (!orig) return D3DERR_INVALIDCALL;
    if (!s.firstPresentLogged) {
        s.firstPresentLogged = true;
        Log("dx9_overlay: Present hook fired (first invocation)");
    }
    RenderFrame(dev);
    return orig(dev, src, dst, wnd, dirty);
}

// Captures the game's real IDirect3DDevice9 the moment it is created, so we hook
// the actual device's Present (no probe device -> no fullscreen-exclusive
// conflict). The IDirect3DDevice9 vtable is shared across every device from
// d3d9.dll, so hooking Present here covers the game's device.
inline HRESULT __stdcall HookedCreateDevice(IDirect3D9* self, UINT adapter, D3DDEVTYPE type,
                                            HWND focusWnd, DWORD flags,
                                            D3DPRESENT_PARAMETERS* pp,
                                            IDirect3DDevice9** outDev) {
    auto& s = State();
    auto orig = s.origCreateDevice;
    if (!orig) return D3DERR_INVALIDCALL;
    HRESULT hr = orig(self, adapter, type, focusWnd, flags, pp, outDev);
    if (SUCCEEDED(hr) && outDev && *outDev && !s.deviceCaptured) {
        s.deviceCaptured = true;
        s.deviceVTable = *reinterpret_cast<void***>(*outDev);
        Log("dx9_overlay: captured game device via CreateDevice");

        // IDirect3DDevice9::Present is vtable index 17.
        s.presentTarget = s.deviceVTable[17];
        if (MH_CreateHook(s.presentTarget, &HookedPresent,
                          reinterpret_cast<LPVOID*>(&s.origPresent)) == MH_OK &&
            MH_EnableHook(s.presentTarget) == MH_OK) {
            s.presentHooked = true;
            Log("dx9_overlay: Present hook enabled on real device");
        } else {
            Log("dx9_overlay: Present hook on real device failed");
        }

        if (s.deviceReadyFn) s.deviceReadyFn(s.deviceVTable);
    }
    return hr;
}

} // namespace detail

inline DX9Overlay::~DX9Overlay() { Remove(); }

inline void SetDX9OverlayLogger(DX9LogFn fn) { detail::State().logFn = fn; }

inline void SetDX9DeviceReadyCallback(DX9DeviceReadyFn fn) { detail::State().deviceReadyFn = fn; }

inline bool DX9Overlay::Install() {
    auto& s = detail::State();
    if (s.hookInstalled) {
        // Refused, not silently taken over. The hooks and the callback slot are
        // process-wide but there is exactly one of each, so a second instance
        // claiming them evicts the first: the live overlay stops rendering, and
        // whichever instance is destroyed FIRST tears down the hooks and D3D
        // resources out from under the other. Returning true here also told the
        // caller it had a working overlay when it had stolen someone else's.
        detail::Log("dx9_overlay: Install refused, another DX9Overlay in this "
                    "module already owns the hooks");
        return false;
    }

    // Direct3DCreate9 returns only the IDirect3D9 factory (no device, no adapter
    // exclusivity), so this always succeeds even while the game is fullscreen.
    // Its vtable is shared with the game's IDirect3D9, so hooking CreateDevice
    // here fires when the game creates its device - and we read that device's
    // real vtable to hook Present.
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) { detail::Log("dx9_overlay: Direct3DCreate9 failed"); return false; }
    void** d3dVTable = *reinterpret_cast<void***>(d3d);
    s.createDeviceTarget = d3dVTable[16];  // IDirect3D9::CreateDevice
    d3d->Release();

    if (MH_CreateHook(s.createDeviceTarget, &detail::HookedCreateDevice,
                      reinterpret_cast<LPVOID*>(&s.origCreateDevice)) != MH_OK) {
        detail::Log("dx9_overlay: MH_CreateHook(CreateDevice) failed");
        return false;
    }
    if (MH_EnableHook(s.createDeviceTarget) != MH_OK) {
        detail::Log("dx9_overlay: MH_EnableHook(CreateDevice) failed");
        MH_RemoveHook(s.createDeviceTarget);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(s.callbackMutex);
        s.callback = std::make_shared<const DX9RenderCallback>(m_callback);
    }
    s.hookInstalled = true;
    m_hookInstalled = true;
    detail::Log("dx9_overlay: CreateDevice hook armed (waiting for game device)");
    return true;
}

inline void DX9Overlay::Remove() {
    // Keyed on this instance, not the shared state: an instance that never
    // installed anything must not tear down the live overlay's hooks.
    if (!m_hookInstalled) return;

    auto& s = detail::State();
    // Targets AND trampolines are cleared. MH_DisableHook does NOT drain threads out
    // of the user detour first - it freezes threads and relocates instruction pointers
    // only within the target and trampoline regions, and HookedPresent is arbitrary
    // code MinHook has never heard of, so a thread inside it is invisible.
    // MH_RemoveHook then returns the trampoline to MinHook's pool for the next
    // MH_CreateHook to reuse, so a straggler that skips the null check calls through
    // freed, likely recycled memory. The detours' null branch is the only thing
    // standing between that and a crash.
    if (s.presentHooked) {
        MH_DisableHook(s.presentTarget);
        MH_RemoveHook(s.presentTarget);
        s.presentHooked = false;
        s.presentTarget = nullptr;
        s.origPresent = nullptr;
    }
    if (s.createDeviceTarget) {
        MH_DisableHook(s.createDeviceTarget);
        MH_RemoveHook(s.createDeviceTarget);
        s.createDeviceTarget = nullptr;
        s.origCreateDevice = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(s.callbackMutex);
        s.callback = nullptr;
    }
    s.deviceCaptured = false;
    s.hookInstalled = false;
    m_hookInstalled = false;
}

// The shared slot is only written when this instance owns the hooks, or when nobody
// does. Refusing a second Install() was not enough on its own: the documented usage
// order is SetRenderCallback() then Install(), so instance B evicted A's callback
// BEFORE it was ever told no, and A's overlay went blank while both objects still
// reported success.
inline void DX9Overlay::SetRenderCallback(DX9RenderCallback cb) {
    m_callback = cb;
    auto& s = detail::State();
    if (s.hookInstalled && !m_hookInstalled) {
        detail::Log("dx9_overlay: SetRenderCallback ignored, another overlay owns the hooks");
        return;
    }
    std::lock_guard<std::mutex> lock(s.callbackMutex);
    s.callback = std::make_shared<const DX9RenderCallback>(std::move(cb));
}

#endif // CAMERAUNLOCK_DX9_OVERLAY_IMPLEMENTATION
// Non-implementation TUs see only the declarations above; method definitions
// live in the impl TU and resolve at link time.

} // namespace cameraunlock::rendering
