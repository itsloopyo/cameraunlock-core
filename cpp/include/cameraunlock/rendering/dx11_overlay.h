#pragma once

// DX11 Overlay System
// Minimal-dep DX11 overlay for drawing crosshair-style 2D primitives over a game.
//
// Design goals:
//   - No ImGui, no kiero. Consumers already vendor MinHook for game hooks; reuse it.
//   - Header-only with a single TU defining CAMERAUNLOCK_DX11_OVERLAY_IMPLEMENTATION.
//   - Pixel-space drawing API (top-left = 0,0). The overlay handles NDC conversion.
//
// Required external dependencies (TU with CAMERAUNLOCK_DX11_OVERLAY_IMPLEMENTATION):
//   - <d3d11.h>, <dxgi.h>, <d3dcompiler.h>
//   - <MinHook.h>
//
// Example:
//   DX11Overlay overlay;
//   overlay.SetRenderCallback([](DX11DrawContext& dc) {
//       dc.DrawCross(dc.Width()/2, dc.Height()/2, 12.0f, 0xFFFFFFFF, 1.5f, 4.0f);
//   });
//   overlay.Install();
//   ...
//   overlay.Remove();

#include <cstdint>
#include <functional>
#include <vector>

namespace cameraunlock::rendering {

// 0xAABBGGRR (D3D11 R8G8B8A8_UNORM with little-endian byte order in memory).
using Rgba = uint32_t;

// Vertex emitted to the GPU. Pixel coords + packed color.
struct DX11OverlayVertex {
    float x;
    float y;
    Rgba  color;
};

// Drawing context passed to the render callback. Accumulates primitives into
// CPU-side vectors; the overlay flushes them once per frame.
class DX11DrawContext {
public:
    DX11DrawContext(float w, float h) : m_width(w), m_height(h) {}

    float Width()  const { return m_width;  }
    float Height() const { return m_height; }

    void DrawLine(float x1, float y1, float x2, float y2, Rgba color, float thickness = 1.0f);
    void DrawRect(float x, float y, float w, float h, Rgba color);
    void DrawDot(float cx, float cy, float radius, Rgba color);

    // Crosshair: 4 line segments centred at (cx, cy), each `arm` long with a
    // central `gap` left empty.
    void DrawCross(float cx, float cy, float arm, Rgba color, float thickness = 1.0f, float gap = 0.0f);

    const std::vector<DX11OverlayVertex>& TriVerts()  const { return m_triVerts;  }

private:
    float m_width;
    float m_height;
    std::vector<DX11OverlayVertex> m_triVerts;  // triangle list
};

using DX11RenderCallback = std::function<void(DX11DrawContext&)>;

// Optional diagnostic log sink. Format string is printf-style.
using DX11LogFn = void (*)(const char* msg);
void SetDX11OverlayLogger(DX11LogFn fn);

class DX11Overlay {
public:
    DX11Overlay() = default;
    ~DX11Overlay();
    DX11Overlay(const DX11Overlay&)            = delete;
    DX11Overlay& operator=(const DX11Overlay&) = delete;

    // Install the Present/ResizeBuffers hooks. Caller must have already
    // initialized MinHook (MH_Initialize). Returns false on failure.
    bool Install();

    // Tear down hooks and release D3D11 resources.
    void Remove();

    void SetRenderCallback(DX11RenderCallback cb);

    bool IsInstalled() const { return m_hookInstalled; }

private:
    DX11RenderCallback m_callback;
    bool m_hookInstalled = false;
};

#ifdef CAMERAUNLOCK_DX11_OVERLAY_IMPLEMENTATION

// ============================================================================
// Implementation
// ============================================================================

} // namespace cameraunlock::rendering - re-opened after includes

#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <MinHook.h>
#include <Windows.h>
#include <cstring>
#include <cmath>
#include <memory>
#include <mutex>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace cameraunlock::rendering {

// ---------- DX11DrawContext ---------------------------------------------------

inline void DX11DrawContext::DrawRect(float x, float y, float w, float h, Rgba color) {
    // Two triangles (CCW with screen-space y-down would be CW; we disable culling so it doesn't matter).
    DX11OverlayVertex v0{x,     y,     color};
    DX11OverlayVertex v1{x + w, y,     color};
    DX11OverlayVertex v2{x + w, y + h, color};
    DX11OverlayVertex v3{x,     y + h, color};
    m_triVerts.push_back(v0); m_triVerts.push_back(v1); m_triVerts.push_back(v2);
    m_triVerts.push_back(v0); m_triVerts.push_back(v2); m_triVerts.push_back(v3);
}

inline void DX11DrawContext::DrawLine(float x1, float y1, float x2, float y2, Rgba color, float thickness) {
    // Render a line as a thin quad so we get reliable thickness without relying on
    // line-list rasterisation (which is 1-pixel only on most adapters).
    float dx = x2 - x1, dy = y2 - y1;
    float len = std::sqrt(dx*dx + dy*dy);
    if (len < 1e-3f) return;
    float nx = -dy / len, ny = dx / len;     // perpendicular
    float t = thickness * 0.5f;
    float ox = nx * t, oy = ny * t;

    DX11OverlayVertex a{x1 - ox, y1 - oy, color};
    DX11OverlayVertex b{x2 - ox, y2 - oy, color};
    DX11OverlayVertex c{x2 + ox, y2 + oy, color};
    DX11OverlayVertex d{x1 + ox, y1 + oy, color};
    m_triVerts.push_back(a); m_triVerts.push_back(b); m_triVerts.push_back(c);
    m_triVerts.push_back(a); m_triVerts.push_back(c); m_triVerts.push_back(d);
}

inline void DX11DrawContext::DrawDot(float cx, float cy, float radius, Rgba color) {
    constexpr int kSegments = 16;
    constexpr float kTau = 6.28318530718f;
    DX11OverlayVertex centre{cx, cy, color};
    for (int i = 0; i < kSegments; ++i) {
        float a0 = (kTau * i) / kSegments;
        float a1 = (kTau * (i + 1)) / kSegments;
        DX11OverlayVertex p0{cx + std::cos(a0) * radius, cy + std::sin(a0) * radius, color};
        DX11OverlayVertex p1{cx + std::cos(a1) * radius, cy + std::sin(a1) * radius, color};
        m_triVerts.push_back(centre);
        m_triVerts.push_back(p0);
        m_triVerts.push_back(p1);
    }
}

inline void DX11DrawContext::DrawCross(float cx, float cy, float arm, Rgba color, float thickness, float gap) {
    if (arm <= gap) return;
    DrawLine(cx - arm, cy, cx - gap, cy, color, thickness);
    DrawLine(cx + gap, cy, cx + arm, cy, color, thickness);
    DrawLine(cx, cy - arm, cx, cy - gap, color, thickness);
    DrawLine(cx, cy + gap, cx, cy + arm, color, thickness);
}

// ---------- DX11Overlay -------------------------------------------------------
//
// Singleton-by-design: the Present hook is a free function, so we keep one
// active overlay at a time. If a future mod needs multiple, this can grow into
// a registry - but every mod we ship has exactly one HUD.

namespace detail {

struct OverlayState {
    // Hook
    bool hookInstalled = false;
    bool initialized   = false;
    void* presentTarget       = nullptr;
    void* resizeBuffersTarget = nullptr;
    HRESULT (__stdcall* origPresent)(IDXGISwapChain*, UINT, UINT) = nullptr;
    HRESULT (__stdcall* origResize)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT) = nullptr;

    // D3D11
    ID3D11Device*           device          = nullptr;
    ID3D11DeviceContext*    context         = nullptr;
    ID3D11RenderTargetView* rtv             = nullptr;
    ID3D11VertexShader*     vs              = nullptr;
    ID3D11PixelShader*      ps              = nullptr;
    ID3D11InputLayout*      inputLayout     = nullptr;
    ID3D11Buffer*           vb              = nullptr;
    UINT                    vbCapacity      = 0;
    ID3D11Buffer*           cb              = nullptr;
    UINT                    cbW             = 0;   // viewport the cb currently holds
    UINT                    cbH             = 0;
    ID3D11BlendState*       blendState      = nullptr;
    ID3D11RasterizerState*  rasterState     = nullptr;
    ID3D11DepthStencilState* depthState     = nullptr;
    UINT                    backbufferW     = 0;
    UINT                    backbufferH     = 0;

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
    std::shared_ptr<const DX11RenderCallback> callback;
    std::mutex         callbackMutex;
    DX11LogFn          logFn = nullptr;
    bool               firstPresentLogged = false;
};

// Viewport half-extents for the vertex shader. 16 bytes, the constant-buffer
// alignment unit.
struct OverlayCB {
    float invHalfW;
    float invHalfH;
    float pad0;
    float pad1;
};

inline OverlayState& State() {
    static OverlayState s;
    return s;
}

inline void Log(const char* msg) {
    auto& s = State();
    if (s.logFn) s.logFn(msg);
}

// Vertex shader: takes pixel coords, viewport size in cb0, outputs NDC.
// Pixel shader: passthrough vertex color.
inline const char* kOverlayHLSL = R"(
cbuffer cb : register(b0) { float2 g_invHalfViewport; float2 _pad; };
struct VSIn  { float2 pos : POSITION; float4 col : COLOR0; };
struct VSOut { float4 pos : SV_POSITION; float4 col : COLOR0; };
VSOut VSMain(VSIn i) {
    VSOut o;
    // Pixel (0..W, 0..H) -> NDC (-1..1, 1..-1)
    o.pos = float4(i.pos.x * g_invHalfViewport.x - 1.0,
                   1.0 - i.pos.y * g_invHalfViewport.y, 0, 1);
    o.col = i.col;
    return o;
}
float4 PSMain(VSOut i) : SV_TARGET { return i.col; }
)";

inline bool CompileShaders(ID3D11Device* dev, ID3D11VertexShader** vs, ID3D11PixelShader** ps, ID3D11InputLayout** layout) {
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* err    = nullptr;

    HRESULT hr = D3DCompile(kOverlayHLSL, std::strlen(kOverlayHLSL), nullptr, nullptr, nullptr,
                            "VSMain", "vs_4_0", 0, 0, &vsBlob, &err);
    if (err) { err->Release(); err = nullptr; }
    if (FAILED(hr)) return false;

    hr = D3DCompile(kOverlayHLSL, std::strlen(kOverlayHLSL), nullptr, nullptr, nullptr,
                    "PSMain", "ps_4_0", 0, 0, &psBlob, &err);
    if (err) { err->Release(); err = nullptr; }
    if (FAILED(hr)) { vsBlob->Release(); return false; }

    hr = dev->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, vs);
    if (FAILED(hr)) { vsBlob->Release(); psBlob->Release(); return false; }

    hr = dev->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, ps);
    if (FAILED(hr)) { vsBlob->Release(); psBlob->Release(); return false; }

    D3D11_INPUT_ELEMENT_DESC inputDesc[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,  0, 8,  D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    hr = dev->CreateInputLayout(inputDesc, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), layout);
    vsBlob->Release(); psBlob->Release();
    return SUCCEEDED(hr);
}

inline void ReleaseDeviceResources();  // forward decl

inline bool InitDeviceResources(IDXGISwapChain* swap) {
    auto& s = State();
    // Always start clean - a previous partial init may have left COM objects behind.
    ReleaseDeviceResources();

    HRESULT hr = swap->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&s.device));
    if (FAILED(hr) || !s.device) {
        Log("dx11_overlay: swap->GetDevice failed");
        ReleaseDeviceResources(); return false;
    }
    s.device->GetImmediateContext(&s.context);

    DXGI_SWAP_CHAIN_DESC desc;
    swap->GetDesc(&desc);
    s.backbufferW = desc.BufferDesc.Width;
    s.backbufferH = desc.BufferDesc.Height;

    // Render target view from back buffer
    ID3D11Texture2D* backbuf = nullptr;
    hr = swap->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backbuf));
    if (FAILED(hr) || !backbuf) {
        Log("dx11_overlay: GetBuffer(0) failed");
        ReleaseDeviceResources(); return false;
    }
    hr = s.device->CreateRenderTargetView(backbuf, nullptr, &s.rtv);
    backbuf->Release();
    if (FAILED(hr)) {
        Log("dx11_overlay: CreateRenderTargetView failed");
        ReleaseDeviceResources(); return false;
    }

    if (!CompileShaders(s.device, &s.vs, &s.ps, &s.inputLayout)) {
        Log("dx11_overlay: shader compilation failed");
        ReleaseDeviceResources(); return false;
    }

    // Dynamic vertex buffer
    s.vbCapacity = 8192;
    D3D11_BUFFER_DESC bd = {};
    bd.Usage          = D3D11_USAGE_DYNAMIC;
    bd.ByteWidth      = sizeof(DX11OverlayVertex) * s.vbCapacity;
    bd.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(s.device->CreateBuffer(&bd, nullptr, &s.vb))) {
        Log("dx11_overlay: vertex-buffer create failed");
        ReleaseDeviceResources(); return false;
    }

    // One dynamic constant buffer for the lifetime of the device resources. The
    // previous per-frame IMMUTABLE buffer was a driver allocation on the hot path.
    D3D11_BUFFER_DESC cbd = {};
    cbd.Usage          = D3D11_USAGE_DYNAMIC;
    cbd.ByteWidth      = sizeof(OverlayCB);
    cbd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(s.device->CreateBuffer(&cbd, nullptr, &s.cb))) {
        Log("dx11_overlay: constant-buffer create failed");
        ReleaseDeviceResources(); return false;
    }
    s.cbW = 0;
    s.cbH = 0;

    // Standard alpha-blended state
    D3D11_BLEND_DESC blend = {};
    blend.RenderTarget[0].BlendEnable           = TRUE;
    blend.RenderTarget[0].SrcBlend              = D3D11_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend             = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha         = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha        = D3D11_BLEND_ZERO;
    blend.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(s.device->CreateBlendState(&blend, &s.blendState))) {
        Log("dx11_overlay: blend state create failed");
        ReleaseDeviceResources(); return false;
    }

    D3D11_RASTERIZER_DESC raster = {};
    raster.FillMode        = D3D11_FILL_SOLID;
    raster.CullMode        = D3D11_CULL_NONE;
    raster.DepthClipEnable = FALSE;
    if (FAILED(s.device->CreateRasterizerState(&raster, &s.rasterState))) {
        Log("dx11_overlay: rasterizer state create failed");
        ReleaseDeviceResources(); return false;
    }

    D3D11_DEPTH_STENCIL_DESC depth = {};
    depth.DepthEnable    = FALSE;
    depth.StencilEnable  = FALSE;
    if (FAILED(s.device->CreateDepthStencilState(&depth, &s.depthState))) {
        Log("dx11_overlay: depth-stencil state create failed");
        ReleaseDeviceResources(); return false;
    }

    s.initialized = true;
    Log("dx11_overlay: device resources initialized");
    return true;
}

inline void ReleaseDeviceResources() {
    auto& s = State();
    if (s.depthState)   { s.depthState->Release();   s.depthState = nullptr; }
    if (s.rasterState)  { s.rasterState->Release();  s.rasterState = nullptr; }
    if (s.blendState)   { s.blendState->Release();   s.blendState = nullptr; }
    if (s.cb)           { s.cb->Release();           s.cb = nullptr; }
    if (s.vb)           { s.vb->Release();           s.vb = nullptr; }
    if (s.inputLayout)  { s.inputLayout->Release();  s.inputLayout = nullptr; }
    if (s.ps)           { s.ps->Release();           s.ps = nullptr; }
    if (s.vs)           { s.vs->Release();           s.vs = nullptr; }
    if (s.rtv)          { s.rtv->Release();          s.rtv = nullptr; }
    if (s.context)      { s.context->Release();      s.context = nullptr; }
    if (s.device)       { s.device->Release();       s.device = nullptr; }
    s.initialized = false;
}

// Save/restore enough of the immediate context that the game's pipeline isn't
// disturbed. We touch: IA (layout, VB, topology), VS, PS, RS, OM (RTV, blend,
// depth), VP. Everything else (HS/DS/GS/CS, SRVs, samplers) is left untouched.
struct ContextStateScope {
    ID3D11DeviceContext* ctx;

    ID3D11InputLayout*   inputLayout = nullptr;
    ID3D11Buffer*        vb          = nullptr;
    UINT                 stride = 0, offset = 0;
    D3D11_PRIMITIVE_TOPOLOGY topology;
    ID3D11VertexShader*  vs          = nullptr;
    ID3D11PixelShader*   ps          = nullptr;
    ID3D11Buffer*        vsCB        = nullptr;
    ID3D11RasterizerState* rs        = nullptr;
    ID3D11BlendState*    blend       = nullptr;
    FLOAT                blendFactor[4] = {};
    UINT                 sampleMask  = 0;
    ID3D11DepthStencilState* depth   = nullptr;
    UINT                 stencilRef  = 0;
    ID3D11RenderTargetView* rtv      = nullptr;
    ID3D11DepthStencilView* dsv      = nullptr;
    UINT                 numVPs      = 1;
    D3D11_VIEWPORT       vp          = {};

    explicit ContextStateScope(ID3D11DeviceContext* c) : ctx(c) {
        ctx->IAGetInputLayout(&inputLayout);
        ctx->IAGetVertexBuffers(0, 1, &vb, &stride, &offset);
        ctx->IAGetPrimitiveTopology(&topology);
        ctx->VSGetShader(&vs, nullptr, nullptr);
        ctx->VSGetConstantBuffers(0, 1, &vsCB);
        ctx->PSGetShader(&ps, nullptr, nullptr);
        ctx->RSGetState(&rs);
        ctx->OMGetBlendState(&blend, blendFactor, &sampleMask);
        ctx->OMGetDepthStencilState(&depth, &stencilRef);
        ctx->OMGetRenderTargets(1, &rtv, &dsv);
        ctx->RSGetViewports(&numVPs, &vp);
    }

    ~ContextStateScope() {
        ctx->IASetInputLayout(inputLayout);
        UINT s = stride, o = offset;
        ctx->IASetVertexBuffers(0, 1, &vb, &s, &o);
        ctx->IASetPrimitiveTopology(topology);
        ctx->VSSetShader(vs, nullptr, 0);
        ctx->VSSetConstantBuffers(0, 1, &vsCB);
        ctx->PSSetShader(ps, nullptr, 0);
        ctx->RSSetState(rs);
        ctx->OMSetBlendState(blend, blendFactor, sampleMask);
        ctx->OMSetDepthStencilState(depth, stencilRef);
        ctx->OMSetRenderTargets(1, &rtv, dsv);
        if (numVPs) ctx->RSSetViewports(numVPs, &vp);

        if (inputLayout) inputLayout->Release();
        if (vb)          vb->Release();
        if (vs)          vs->Release();
        if (vsCB)        vsCB->Release();
        if (ps)          ps->Release();
        if (rs)          rs->Release();
        if (blend)       blend->Release();
        if (depth)       depth->Release();
        if (rtv)         rtv->Release();
        if (dsv)         dsv->Release();
    }
};

inline void RenderFrame() {
    auto& s = State();
    if (!s.initialized) return;
    if (s.backbufferW == 0 || s.backbufferH == 0) return;

    std::shared_ptr<const DX11RenderCallback> callback;
    {
        std::lock_guard<std::mutex> lock(s.callbackMutex);
        callback = s.callback;
    }
    if (!callback || !*callback) return;

    DX11DrawContext dc(static_cast<float>(s.backbufferW), static_cast<float>(s.backbufferH));
    (*callback)(dc);
    const auto& verts = dc.TriVerts();
    if (verts.empty()) return;

    UINT needed = static_cast<UINT>(verts.size());
    if (needed > s.vbCapacity) {
        // Grow VB. Round up to next 8K block.
        if (s.vb) { s.vb->Release(); s.vb = nullptr; }
        UINT newCap = ((needed + 8191u) / 8192u) * 8192u;
        D3D11_BUFFER_DESC bd = {};
        bd.Usage          = D3D11_USAGE_DYNAMIC;
        bd.ByteWidth      = sizeof(DX11OverlayVertex) * newCap;
        bd.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(s.device->CreateBuffer(&bd, nullptr, &s.vb))) return;
        s.vbCapacity = newCap;
    }

    // Upload verts
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(s.context->Map(s.vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
    std::memcpy(mapped.pData, verts.data(), sizeof(DX11OverlayVertex) * needed);
    s.context->Unmap(s.vb, 0);

    // Refresh the viewport constants only when the backbuffer actually changes.
    if (s.cbW != s.backbufferW || s.cbH != s.backbufferH) {
        D3D11_MAPPED_SUBRESOURCE cbMapped = {};
        if (FAILED(s.context->Map(s.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &cbMapped))) return;
        OverlayCB cbData = { 2.0f / s.backbufferW, 2.0f / s.backbufferH, 0, 0 };
        std::memcpy(cbMapped.pData, &cbData, sizeof(cbData));
        s.context->Unmap(s.cb, 0);
        s.cbW = s.backbufferW;
        s.cbH = s.backbufferH;
    }

    {
        ContextStateScope save(s.context);

        D3D11_VIEWPORT vp = {};
        vp.TopLeftX = 0; vp.TopLeftY = 0;
        vp.Width    = static_cast<float>(s.backbufferW);
        vp.Height   = static_cast<float>(s.backbufferH);
        vp.MinDepth = 0; vp.MaxDepth = 1;
        s.context->RSSetViewports(1, &vp);

        s.context->OMSetRenderTargets(1, &s.rtv, nullptr);

        UINT stride = sizeof(DX11OverlayVertex), offset = 0;
        s.context->IASetInputLayout(s.inputLayout);
        s.context->IASetVertexBuffers(0, 1, &s.vb, &stride, &offset);
        s.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        s.context->VSSetShader(s.vs, nullptr, 0);
        s.context->VSSetConstantBuffers(0, 1, &s.cb);
        s.context->PSSetShader(s.ps, nullptr, 0);
        s.context->RSSetState(s.rasterState);
        FLOAT bf[4] = {0,0,0,0};
        s.context->OMSetBlendState(s.blendState, bf, 0xFFFFFFFFu);
        s.context->OMSetDepthStencilState(s.depthState, 0);

        s.context->Draw(needed, 0);
    }
}

inline HRESULT __stdcall HookedPresent(IDXGISwapChain* swap, UINT sync, UINT flags) {
    auto& s = State();
    // REACHABLE. MH_DisableHook restores the original bytes, but it does not drain
    // threads already inside this detour - it relocates instruction pointers only
    // within the target and trampoline regions, and this function is not in either.
    // A thread that entered before Remove() and reaches here afterwards finds the
    // trampoline nulled, which is the point: the alternative is calling through
    // memory MH_RemoveHook has already returned to MinHook's pool. Returning an
    // error is correct - the frame genuinely was not presented.
    auto orig = s.origPresent;
    if (!orig) return DXGI_ERROR_INVALID_CALL;
    if (!s.firstPresentLogged) {
        s.firstPresentLogged = true;
        Log("dx11_overlay: Present hook fired (first invocation)");
    }
    if (!s.initialized) {
        InitDeviceResources(swap);
    }
    if (s.initialized) {
        RenderFrame();
    }
    return orig(swap, sync, flags);
}

inline HRESULT __stdcall HookedResizeBuffers(IDXGISwapChain* swap, UINT bufferCount, UINT width, UINT height,
                                             DXGI_FORMAT format, UINT swapChainFlags) {
    auto& s = State();
    // See HookedPresent. Returning S_OK here would be the worse lie of the two: the
    // engine would believe the swap chain resized and go on to use buffers that are
    // still the old size.
    auto orig = s.origResize;
    if (!orig) return DXGI_ERROR_INVALID_CALL;
    if (s.initialized) {
        // Drop view + remaining device-bound buffers; they'll be recreated on next Present.
        if (s.rtv) { s.rtv->Release(); s.rtv = nullptr; }
        s.initialized = false;
    }
    return orig(swap, bufferCount, width, height, format, swapChainFlags);
}

// Get the IDXGISwapChain vtable by spawning a tiny temp swap chain.
inline bool GetSwapChainVTable(void**& outVTable) {
    WNDCLASSEXA wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = DefWindowProcA;
    wc.hInstance     = GetModuleHandleA(nullptr);
    wc.lpszClassName = "_CUL_OverlayProbe";
    if (!RegisterClassExA(&wc)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    }
    HWND hwnd = CreateWindowA("_CUL_OverlayProbe", "_probe", WS_POPUP, 0, 0, 16, 16,
                              nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) return false;

    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferDesc.Width            = 16;
    scd.BufferDesc.Height           = 16;
    scd.BufferDesc.RefreshRate      = {60, 1};
    scd.BufferDesc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.SampleDesc                  = {1, 0};
    scd.BufferUsage                 = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount                 = 1;
    scd.OutputWindow                = hwnd;
    scd.Windowed                    = TRUE;
    scd.SwapEffect                  = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL lvl = D3D_FEATURE_LEVEL_11_0;
    IDXGISwapChain*      swap   = nullptr;
    ID3D11Device*        dev    = nullptr;
    ID3D11DeviceContext* ctx    = nullptr;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &lvl, 1,
        D3D11_SDK_VERSION, &scd, &swap, &dev, nullptr, &ctx);
    if (FAILED(hr)) {
        DestroyWindow(hwnd);
        return false;
    }

    outVTable = *reinterpret_cast<void***>(swap);
    swap->Release();
    if (ctx) ctx->Release();
    if (dev) dev->Release();
    DestroyWindow(hwnd);
    return true;
}

} // namespace detail

inline DX11Overlay::~DX11Overlay() { Remove(); }

inline void SetDX11OverlayLogger(DX11LogFn fn) { detail::State().logFn = fn; }

inline bool DX11Overlay::Install() {
    auto& s = detail::State();

    // Idempotent for the instance that already owns the hooks. Both earlier versions
    // returned true here, and a consumer doing lazy-retry init or re-installing on a
    // config reload would otherwise disable its own working HUD on the second call -
    // while IsInstalled() kept reporting true, so the two accessors disagreed.
    if (m_hookInstalled) return true;

    if (s.hookInstalled) {
        // Refused, not silently taken over. The hooks and the callback slot are
        // process-wide but there is exactly one of each, so a second instance
        // claiming them evicts the first: the live overlay stops rendering, and
        // whichever instance is destroyed FIRST tears down the hooks and D3D
        // resources out from under the other. Returning true here also told the
        // caller it had a working overlay when it had stolen someone else's.
        detail::Log("dx11_overlay: Install refused, another DX11Overlay in this "
                    "module already owns the hooks");
        return false;
    }

    void** vtable = nullptr;
    if (!detail::GetSwapChainVTable(vtable)) {
        detail::Log("dx11_overlay: GetSwapChainVTable failed (no probe device?)");
        return false;
    }
    detail::Log("dx11_overlay: swap chain vtable obtained");

    // IDXGISwapChain (DXGI 1.0): Present @ 8, ResizeBuffers @ 13.
    s.presentTarget       = vtable[8];
    s.resizeBuffersTarget = vtable[13];

    if (MH_CreateHook(s.presentTarget, &detail::HookedPresent,
                      reinterpret_cast<LPVOID*>(&s.origPresent)) != MH_OK) {
        detail::Log("dx11_overlay: MH_CreateHook(Present) failed");
        return false;
    }
    if (MH_CreateHook(s.resizeBuffersTarget, &detail::HookedResizeBuffers,
                      reinterpret_cast<LPVOID*>(&s.origResize)) != MH_OK) {
        detail::Log("dx11_overlay: MH_CreateHook(ResizeBuffers) failed");
        MH_RemoveHook(s.presentTarget);
        return false;
    }
    if (MH_EnableHook(s.presentTarget) != MH_OK ||
        MH_EnableHook(s.resizeBuffersTarget) != MH_OK) {
        detail::Log("dx11_overlay: MH_EnableHook failed");
        MH_RemoveHook(s.presentTarget);
        MH_RemoveHook(s.resizeBuffersTarget);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(s.callbackMutex);
        s.callback = std::make_shared<const DX11RenderCallback>(m_callback);
    }
    s.hookInstalled = true;
    m_hookInstalled = true;
    detail::Log("dx11_overlay: hooks enabled");
    return true;
}

inline void DX11Overlay::Remove() {
    // Keyed on this instance, not the shared state: an instance that never
    // installed anything must not tear down the live overlay's hooks and
    // release its D3D resources when it goes out of scope.
    if (!m_hookInstalled) return;

    auto& s = detail::State();
    // NULL-GUARDED. MH_ALL_HOOKS is defined as NULL, so passing a null target here
    // disables and removes EVERY MinHook hook in the process - the camera hook, the
    // discovery probes, other mods' hooks. That is reachable with two overlay
    // instances: the first Remove() nulls the shared targets, the second passes null.
    // dx9_overlay.h has always guarded these; this side did not.
    if (s.presentTarget) {
        MH_DisableHook(s.presentTarget);
        MH_RemoveHook(s.presentTarget);
    }
    if (s.resizeBuffersTarget) {
        MH_DisableHook(s.resizeBuffersTarget);
        MH_RemoveHook(s.resizeBuffersTarget);
    }

    detail::ReleaseDeviceResources();
    {
        std::lock_guard<std::mutex> lock(s.callbackMutex);
        s.callback = nullptr;
    }
    // BOTH the targets and the trampolines are cleared. The targets so a second
    // Remove() cannot pass null to MinHook (above), and the trampolines because
    // MH_DisableHook does NOT drain threads out of the user detour first - it freezes
    // threads and relocates instruction pointers only within the target and trampoline
    // regions, and HookedPresent is arbitrary code MinHook has never heard of, so a
    // thread inside it is invisible. MH_RemoveHook then returns the trampoline to
    // MinHook's pool for the next MH_CreateHook to reuse. A straggler that skips the
    // null check therefore calls through freed, likely recycled memory; the detours'
    // null branch is the only thing standing between that and a crash.
    s.presentTarget = nullptr;
    s.resizeBuffersTarget = nullptr;
    s.origPresent = nullptr;
    s.origResize = nullptr;
    s.hookInstalled = false;
    m_hookInstalled = false;
}

// Update the cached callback whenever consumer changes it after Install.
// (m_callback is the source of truth on the public object, but RenderFrame
// reads the State() copy to avoid touching this-pointer in a free function.)
//
// The shared slot is only written when this instance owns the hooks, or when nobody
// does. Refusing a second Install() was not enough on its own: the documented usage
// order is SetRenderCallback() then Install(), so instance B evicted A's callback
// BEFORE it was ever told no, and A's overlay went blank while both objects still
// reported success.
inline void DX11Overlay::SetRenderCallback(DX11RenderCallback cb) {
    m_callback = cb;
    auto& s = detail::State();
    if (s.hookInstalled && !m_hookInstalled) {
        detail::Log("dx11_overlay: SetRenderCallback ignored, another overlay owns the hooks");
        return;
    }
    std::lock_guard<std::mutex> lock(s.callbackMutex);
    s.callback = std::make_shared<const DX11RenderCallback>(std::move(cb));
}

#endif // CAMERAUNLOCK_DX11_OVERLAY_IMPLEMENTATION
// Non-implementation TUs see only the declarations above; method definitions
// live in the impl TU and resolve at link time.

} // namespace cameraunlock::rendering
