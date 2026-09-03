#pragma once

// DX12 Overlay System
// Minimal-dep DX12 overlay for drawing crosshair-style 2D primitives over a game.
//
// The sibling of dx11_overlay.h, with the same public shape and the same
// primitives (overlay_draw_list.h), so a consumer swaps backend by swapping a
// type name and nothing else. That is the whole point of it: aim_marker.h is
// written once against DrawCross, and a mod picks the backend its player's
// renderer actually is.
//
// Design goals:
//   - No ImGui, no kiero. Consumers already vendor MinHook for game hooks; reuse it.
//   - Header-only with a single TU defining CAMERAUNLOCK_DX12_OVERLAY_IMPLEMENTATION.
//   - Pixel-space drawing API (top-left = 0,0). The overlay handles NDC conversion.
//
// Required external dependencies (TU with CAMERAUNLOCK_DX12_OVERLAY_IMPLEMENTATION):
//   - <d3d12.h>, <dxgi1_4.h>, <d3dcompiler.h>
//   - <MinHook.h>
//
// What D3D12 needs that D3D11 did not:
//
//   - **The command queue.** D3D11 hands a device straight off the swap chain
//     and an immediate context with it. D3D12 has no immediate context and the
//     swap chain will not name the queue it was created against, so the only way
//     to reach it is to watch it being used: ExecuteCommandLists is hooked purely
//     to record the first DIRECT queue that passes through. Until one does, the
//     overlay has nowhere to submit and draws nothing.
//   - **Per-back-buffer state.** One command allocator per buffer, and a fence
//     value per buffer, because an allocator cannot be reset while the GPU is
//     still reading the commands it holds.
//   - **Explicit transitions.** The back buffer arrives at Present in the
//     PRESENT state and has to be handed back in it.
//
// Example:
//   DX12Overlay overlay;
//   overlay.SetRenderCallback([](DX12DrawContext& dc) {
//       dc.DrawCross(dc.Width()/2, dc.Height()/2, 12.0f, 0xFFFFFFFF, 1.5f, 4.0f);
//   });
//   overlay.Install();
//   ...
//   overlay.Remove();

#include <functional>

#include "cameraunlock/rendering/overlay_draw_list.h"

namespace cameraunlock::rendering {

// Shared with the D3D11 backend, so a render callback keeps its shape across
// both and one marker implementation drives either.
using DX12OverlayVertex = OverlayVertex;
using DX12DrawContext   = OverlayDrawList;

using DX12RenderCallback = std::function<void(DX12DrawContext&)>;

// Optional diagnostic log sink.
using DX12LogFn = OverlayLogFn;
void SetDX12OverlayLogger(DX12LogFn fn);

class DX12Overlay {
public:
    DX12Overlay() = default;
    ~DX12Overlay();
    DX12Overlay(const DX12Overlay&)            = delete;
    DX12Overlay& operator=(const DX12Overlay&) = delete;

    // Install the Present/ResizeBuffers/ExecuteCommandLists hooks. Caller must
    // have already initialized MinHook (MH_Initialize). Returns false on failure.
    bool Install();

    // Tear down hooks and release D3D12 resources.
    void Remove();

    void SetRenderCallback(DX12RenderCallback cb);

    bool IsInstalled() const { return m_hookInstalled; }

private:
    DX12RenderCallback m_callback;
    bool m_hookInstalled = false;
};

#ifdef CAMERAUNLOCK_DX12_OVERLAY_IMPLEMENTATION

// ============================================================================
// Implementation
// ============================================================================

} // namespace cameraunlock::rendering - re-opened after includes

#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <MinHook.h>
#include <Windows.h>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace cameraunlock::rendering {

namespace detail12 {

using Present_t = HRESULT (__stdcall*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffers_t = HRESULT (__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using ExecuteCommandLists_t = void (__stdcall*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);

// One back buffer's worth of per-frame state.
struct FrameContext {
    ID3D12Resource*             backBuffer = nullptr;
    ID3D12CommandAllocator*     allocator  = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv        = {};
    // The fence value our last submission for THIS buffer signalled. The
    // allocator cannot be reset until the GPU has passed it.
    UINT64                      fenceValue = 0;
};

struct OverlayState {
    // Hooks
    bool  hookInstalled = false;
    void* presentTarget = nullptr;
    void* resizeTarget  = nullptr;
    void* executeTarget = nullptr;
    Present_t             origPresent = nullptr;
    ResizeBuffers_t       origResize  = nullptr;
    ExecuteCommandLists_t origExecute = nullptr;

    // The game's own DIRECT queue, learned from the ExecuteCommandLists detour.
    // Atomic because that detour is the render thread and Install/Remove are not.
    // AddRef'd on capture: a queue released underneath us would be submitted to
    // for the rest of the session.
    std::atomic<ID3D12CommandQueue*> queue{nullptr};

    // Device resources
    bool                       initialized = false;
    ID3D12Device*              device      = nullptr;
    ID3D12DescriptorHeap*      rtvHeap     = nullptr;
    ID3D12GraphicsCommandList* cmdList     = nullptr;
    ID3D12RootSignature*       rootSig     = nullptr;
    ID3D12PipelineState*       pso         = nullptr;
    ID3D12Fence*               fence       = nullptr;
    HANDLE                     fenceEvent  = nullptr;
    UINT64                     fenceValue  = 0;
    std::vector<FrameContext>  frames;

    // Vertex buffer: one UPLOAD-heap resource, mapped once and left mapped. A
    // per-frame Map/Unmap pair is a driver round trip on the render thread, and
    // an UPLOAD resource is CPU-visible for its whole lifetime by design.
    ID3D12Resource*          vb         = nullptr;
    void*                    vbCpu      = nullptr;
    UINT                     vbCapacity = 0;
    D3D12_VERTEX_BUFFER_VIEW vbView     = {};

    UINT width  = 0;
    UINT height = 0;

    // See the DX11 note: held by shared_ptr so the render thread copies a
    // refcount rather than the functor, and a callback swapped from the mod
    // thread cannot tear under an in-flight invocation.
    std::shared_ptr<const DX12RenderCallback> callback;
    std::mutex callbackMutex;

    // The swap chain last found to have no ID3D12Device. Keyed on the pointer
    // rather than a bare flag: in a D3D11 game these hooks sit on the same
    // shared DXGI vtable and would otherwise re-probe on every Present for the
    // life of the process, but a game that destroys its swap chain and builds a
    // new one still gets that one looked at.
    IDXGISwapChain* notD3D12Swap = nullptr;

    DX12LogFn logFn = nullptr;
    bool firstPresentLogged = false;
    bool noQueueLogged      = false;
    bool notD3D12Logged     = false;
};

inline OverlayState& State() {
    static OverlayState s;
    return s;
}

inline void Log(const char* msg) {
    auto& s = State();
    if (s.logFn) s.logFn(msg);
}

// Viewport half-extents for the vertex shader, as root constants. 4 DWORDs,
// matching the cbuffer in kOverlayHLSL.
struct OverlayRootConstants {
    float invHalfW;
    float invHalfH;
    float pad0;
    float pad1;
};

inline void ReleaseDeviceResources();  // forward decl

// Block until the GPU has passed `value`, or until the wait times out. Bounded
// on purpose: this runs on the render thread inside a hooked Present, and an
// INFINITE wait on a fence the game's queue never signals - a device removal, a
// queue that stopped being submitted to - hangs the game outright with the
// overlay as the cause. A timeout instead drops our own frame.
inline bool WaitForFence(UINT64 value, DWORD timeoutMs = 1000) {
    auto& s = State();
    if (!s.fence || value == 0) return true;
    if (s.fence->GetCompletedValue() >= value) return true;
    if (!s.fenceEvent) return false;
    if (FAILED(s.fence->SetEventOnCompletion(value, s.fenceEvent))) return false;
    return WaitForSingleObject(s.fenceEvent, timeoutMs) == WAIT_OBJECT_0;
}

// Drain everything we have submitted. Used before releasing resources the GPU
// may still be reading: a resize, a vertex-buffer grow, and teardown.
inline void FlushGpu() {
    auto& s = State();
    ID3D12CommandQueue* queue = s.queue.load(std::memory_order_acquire);
    if (!queue || !s.fence) return;
    const UINT64 value = ++s.fenceValue;
    if (FAILED(queue->Signal(s.fence, value))) return;
    WaitForFence(value);
}

inline bool CompileShaders(ID3DBlob** vs, ID3DBlob** ps) {
    ID3DBlob* err = nullptr;
    HRESULT hr = D3DCompile(kOverlayHLSL, std::strlen(kOverlayHLSL), nullptr, nullptr, nullptr,
                            "VSMain", "vs_5_0", 0, 0, vs, &err);
    if (err) { err->Release(); err = nullptr; }
    if (FAILED(hr)) return false;

    hr = D3DCompile(kOverlayHLSL, std::strlen(kOverlayHLSL), nullptr, nullptr, nullptr,
                    "PSMain", "ps_5_0", 0, 0, ps, &err);
    if (err) { err->Release(); err = nullptr; }
    if (FAILED(hr)) { (*vs)->Release(); *vs = nullptr; return false; }
    return true;
}

inline bool CreatePipeline(DXGI_FORMAT rtvFormat) {
    auto& s = State();

    // One root parameter: the viewport constants, straight in the root
    // signature. Four DWORDs is well inside the 64-DWORD budget and saves a
    // constant-buffer resource, its heap and its per-frame upload.
    D3D12_ROOT_PARAMETER param = {};
    param.ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    param.Constants.ShaderRegister = 0;
    param.Constants.RegisterSpace  = 0;
    param.Constants.Num32BitValues = 4;
    param.ShaderVisibility         = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters = 1;
    rsDesc.pParameters   = &param;
    rsDesc.Flags         = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ID3DBlob* rsBlob = nullptr;
    ID3DBlob* rsErr  = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                             &rsBlob, &rsErr);
    if (rsErr) { rsErr->Release(); rsErr = nullptr; }
    if (FAILED(hr) || !rsBlob) {
        Log("dx12_overlay: root signature serialisation failed");
        return false;
    }
    hr = s.device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(),
                                       IID_PPV_ARGS(&s.rootSig));
    rsBlob->Release();
    if (FAILED(hr)) {
        Log("dx12_overlay: CreateRootSignature failed");
        return false;
    }

    ID3DBlob* vs = nullptr;
    ID3DBlob* ps = nullptr;
    if (!CompileShaders(&vs, &ps)) {
        Log("dx12_overlay: shader compilation failed");
        return false;
    }

    const D3D12_INPUT_ELEMENT_DESC inputDesc[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.pRootSignature        = s.rootSig;
    pso.VS                    = {vs->GetBufferPointer(), vs->GetBufferSize()};
    pso.PS                    = {ps->GetBufferPointer(), ps->GetBufferSize()};
    pso.InputLayout           = {inputDesc, 2};
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets      = 1;
    // The swap chain's own format, so an sRGB back buffer is written through the
    // same conversion the game's own draws use and the mark is not the one thing
    // on screen at the wrong brightness.
    pso.RTVFormats[0]         = rtvFormat;
    pso.DSVFormat             = DXGI_FORMAT_UNKNOWN;
    pso.SampleDesc            = {1, 0};
    pso.SampleMask            = UINT_MAX;
    pso.NodeMask              = 0;

    pso.RasterizerState.FillMode              = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode              = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable       = FALSE;
    pso.RasterizerState.ConservativeRaster    = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    pso.BlendState.RenderTarget[0].BlendEnable           = TRUE;
    pso.BlendState.RenderTarget[0].SrcBlend              = D3D12_BLEND_SRC_ALPHA;
    pso.BlendState.RenderTarget[0].DestBlend             = D3D12_BLEND_INV_SRC_ALPHA;
    pso.BlendState.RenderTarget[0].BlendOp               = D3D12_BLEND_OP_ADD;
    pso.BlendState.RenderTarget[0].SrcBlendAlpha         = D3D12_BLEND_ONE;
    pso.BlendState.RenderTarget[0].DestBlendAlpha        = D3D12_BLEND_ZERO;
    pso.BlendState.RenderTarget[0].BlendOpAlpha          = D3D12_BLEND_OP_ADD;
    pso.BlendState.RenderTarget[0].LogicOp               = D3D12_LOGIC_OP_NOOP;
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    pso.DepthStencilState.DepthEnable   = FALSE;
    pso.DepthStencilState.StencilEnable = FALSE;

    hr = s.device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&s.pso));
    vs->Release();
    ps->Release();
    if (FAILED(hr)) {
        Log("dx12_overlay: CreateGraphicsPipelineState failed");
        return false;
    }
    return true;
}

inline bool CreateVertexBuffer(UINT capacity) {
    auto& s = State();

    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width            = sizeof(OverlayVertex) * capacity;
    desc.Height           = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels        = 1;
    desc.Format           = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc       = {1, 0};
    desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (FAILED(s.device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, IID_PPV_ARGS(&s.vb)))) {
        Log("dx12_overlay: vertex-buffer create failed");
        return false;
    }
    // Read range {0,0}: the CPU never reads this resource back, and saying so
    // lets the driver skip a cache invalidate on discrete memory.
    D3D12_RANGE readRange = {0, 0};
    if (FAILED(s.vb->Map(0, &readRange, &s.vbCpu))) {
        Log("dx12_overlay: vertex-buffer map failed");
        return false;
    }
    s.vbCapacity          = capacity;
    s.vbView.BufferLocation = s.vb->GetGPUVirtualAddress();
    s.vbView.StrideInBytes  = sizeof(OverlayVertex);
    s.vbView.SizeInBytes    = static_cast<UINT>(desc.Width);
    return true;
}

inline bool InitDeviceResources(IDXGISwapChain* swap) {
    auto& s = State();
    if (swap == s.notD3D12Swap) return false;
    ReleaseDeviceResources();

    IDXGISwapChain3* swap3 = nullptr;
    if (FAILED(swap->QueryInterface(IID_PPV_ARGS(&swap3))) || !swap3) {
        Log("dx12_overlay: swap chain is not an IDXGISwapChain3");
        return false;
    }

    HRESULT hr = swap3->GetDevice(IID_PPV_ARGS(&s.device));
    swap3->Release();
    if (FAILED(hr) || !s.device) {
        // The expected outcome in a D3D11 game: the hooks are on the shared DXGI
        // vtable, so this backend sees every Present in the process whether or
        // not the renderer is the one it can draw on.
        if (!s.notD3D12Logged) {
            s.notD3D12Logged = true;
            Log("dx12_overlay: this swap chain has no ID3D12Device, so the renderer is "
                "not Direct3D 12 and this overlay will not draw");
        }
        ReleaseDeviceResources();
        s.notD3D12Swap = swap;
        return false;
    }

    DXGI_SWAP_CHAIN_DESC desc = {};
    swap->GetDesc(&desc);
    s.width  = desc.BufferDesc.Width;
    s.height = desc.BufferDesc.Height;
    const UINT bufferCount = desc.BufferCount ? desc.BufferCount : 2;

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDesc.NumDescriptors = bufferCount;
    heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(s.device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&s.rtvHeap)))) {
        Log("dx12_overlay: RTV descriptor heap create failed");
        ReleaseDeviceResources();
        return false;
    }
    const UINT rtvStride =
        s.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = s.rtvHeap->GetCPUDescriptorHandleForHeapStart();

    s.frames.resize(bufferCount);
    for (UINT i = 0; i < bufferCount; ++i) {
        FrameContext& f = s.frames[i];
        if (FAILED(swap->GetBuffer(i, IID_PPV_ARGS(&f.backBuffer)))) {
            Log("dx12_overlay: GetBuffer failed");
            ReleaseDeviceResources();
            return false;
        }
        s.device->CreateRenderTargetView(f.backBuffer, nullptr, rtv);
        f.rtv = rtv;
        rtv.ptr += rtvStride;

        if (FAILED(s.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                    IID_PPV_ARGS(&f.allocator)))) {
            Log("dx12_overlay: command allocator create failed");
            ReleaseDeviceResources();
            return false;
        }
        f.fenceValue = 0;
    }

    if (FAILED(s.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           s.frames[0].allocator, nullptr,
                                           IID_PPV_ARGS(&s.cmdList)))) {
        Log("dx12_overlay: command list create failed");
        ReleaseDeviceResources();
        return false;
    }
    // Created open. Every frame resets it, so it starts closed like the rest.
    s.cmdList->Close();

    if (FAILED(s.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&s.fence)))) {
        Log("dx12_overlay: fence create failed");
        ReleaseDeviceResources();
        return false;
    }
    s.fenceValue = 0;
    s.fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!s.fenceEvent) {
        Log("dx12_overlay: fence event create failed");
        ReleaseDeviceResources();
        return false;
    }

    if (!CreatePipeline(desc.BufferDesc.Format)) {
        ReleaseDeviceResources();
        return false;
    }
    if (!CreateVertexBuffer(8192)) {
        ReleaseDeviceResources();
        return false;
    }

    s.notD3D12Swap = nullptr;
    s.initialized = true;
    Log("dx12_overlay: device resources initialized");
    return true;
}

inline void ReleaseDeviceResources() {
    auto& s = State();
    if (s.vb && s.vbCpu) { s.vb->Unmap(0, nullptr); s.vbCpu = nullptr; }
    if (s.vb)         { s.vb->Release();         s.vb = nullptr; }
    s.vbCapacity = 0;
    s.vbView = {};
    if (s.pso)        { s.pso->Release();        s.pso = nullptr; }
    if (s.rootSig)    { s.rootSig->Release();    s.rootSig = nullptr; }
    if (s.cmdList)    { s.cmdList->Release();    s.cmdList = nullptr; }
    for (FrameContext& f : s.frames) {
        if (f.backBuffer) { f.backBuffer->Release(); f.backBuffer = nullptr; }
        if (f.allocator)  { f.allocator->Release();  f.allocator = nullptr; }
    }
    s.frames.clear();
    if (s.rtvHeap)    { s.rtvHeap->Release();    s.rtvHeap = nullptr; }
    if (s.fenceEvent) { CloseHandle(s.fenceEvent); s.fenceEvent = nullptr; }
    if (s.fence)      { s.fence->Release();      s.fence = nullptr; }
    s.fenceValue = 0;
    if (s.device)     { s.device->Release();     s.device = nullptr; }
    s.initialized = false;
}

inline void RenderFrame(IDXGISwapChain* swap) {
    auto& s = State();
    if (!s.initialized) return;
    if (s.width == 0 || s.height == 0) return;

    ID3D12CommandQueue* queue = s.queue.load(std::memory_order_acquire);
    if (!queue) {
        // Nothing has been submitted through our ExecuteCommandLists detour yet.
        // Said once, because it is the difference between "the overlay is broken"
        // and "the overlay has not been handed a queue".
        if (!s.noQueueLogged) {
            s.noQueueLogged = true;
            Log("dx12_overlay: no DIRECT command queue seen yet, nothing drawn this frame");
        }
        return;
    }

    std::shared_ptr<const DX12RenderCallback> callback;
    {
        std::lock_guard<std::mutex> lock(s.callbackMutex);
        callback = s.callback;
    }
    if (!callback || !*callback) return;

    DX12DrawContext dc(static_cast<float>(s.width), static_cast<float>(s.height));
    (*callback)(dc);
    const auto& verts = dc.TriVerts();
    if (verts.empty()) return;

    const UINT needed = static_cast<UINT>(verts.size());
    if (needed > s.vbCapacity) {
        // The GPU may still be reading the buffer we are about to release, so
        // this is one of the three places that drains first.
        FlushGpu();
        if (s.vb && s.vbCpu) { s.vb->Unmap(0, nullptr); s.vbCpu = nullptr; }
        if (s.vb) { s.vb->Release(); s.vb = nullptr; }
        const UINT newCap = ((needed + 8191u) / 8192u) * 8192u;
        if (!CreateVertexBuffer(newCap)) { s.initialized = false; return; }
    }

    IDXGISwapChain3* swap3 = nullptr;
    if (FAILED(swap->QueryInterface(IID_PPV_ARGS(&swap3))) || !swap3) return;
    const UINT index = swap3->GetCurrentBackBufferIndex();
    swap3->Release();
    if (index >= s.frames.size()) return;

    FrameContext& f = s.frames[index];
    // The allocator still holds the commands of the last frame drawn into this
    // buffer. Resetting it before the GPU is done with them corrupts the list
    // being executed, which is the classic D3D12 overlay crash.
    if (!WaitForFence(f.fenceValue)) return;

    std::memcpy(s.vbCpu, verts.data(), sizeof(OverlayVertex) * needed);

    if (FAILED(f.allocator->Reset())) return;
    if (FAILED(s.cmdList->Reset(f.allocator, s.pso))) return;

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = f.backBuffer;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    s.cmdList->ResourceBarrier(1, &barrier);

    s.cmdList->OMSetRenderTargets(1, &f.rtv, FALSE, nullptr);

    D3D12_VIEWPORT vp = {};
    vp.Width    = static_cast<float>(s.width);
    vp.Height   = static_cast<float>(s.height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    s.cmdList->RSSetViewports(1, &vp);

    D3D12_RECT scissor = {0, 0, static_cast<LONG>(s.width), static_cast<LONG>(s.height)};
    s.cmdList->RSSetScissorRects(1, &scissor);

    s.cmdList->SetGraphicsRootSignature(s.rootSig);
    const OverlayRootConstants constants = {2.0f / s.width, 2.0f / s.height, 0.0f, 0.0f};
    s.cmdList->SetGraphicsRoot32BitConstants(0, 4, &constants, 0);

    s.cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    s.cmdList->IASetVertexBuffers(0, 1, &s.vbView);
    s.cmdList->DrawInstanced(needed, 1, 0, 0);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
    s.cmdList->ResourceBarrier(1, &barrier);

    if (FAILED(s.cmdList->Close())) return;

    ID3D12CommandList* lists[] = {s.cmdList};
    // The ORIGINAL, not the detour: our own submission has no queue to learn and
    // going back through the hook only adds a branch to every frame. Read once
    // and null-checked, because Remove() clears it and this runs on the render
    // thread - see the same guard at the top of each detour.
    auto execute = s.origExecute;
    if (!execute) return;
    execute(queue, 1, lists);

    f.fenceValue = ++s.fenceValue;
    queue->Signal(s.fence, f.fenceValue);
}

inline void __stdcall HookedExecuteCommandLists(ID3D12CommandQueue* queue, UINT numLists,
                                                ID3D12CommandList* const* lists) {
    auto& s = State();
    auto orig = s.origExecute;
    // See dx11_overlay.h's HookedPresent for why a null trampoline is reachable
    // and why returning early is the right answer. Here the cost of the early
    // return is the game's own command lists never being submitted, so this
    // branch is only taken after Remove() has already stopped the world for us.
    if (!orig) return;

    if (!s.queue.load(std::memory_order_acquire)) {
        const D3D12_COMMAND_QUEUE_DESC desc = queue->GetDesc();
        if (desc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
            ID3D12CommandQueue* expected = nullptr;
            queue->AddRef();
            if (!s.queue.compare_exchange_strong(expected, queue,
                                                 std::memory_order_acq_rel)) {
                // Another thread won the race; drop the reference we took.
                queue->Release();
            }
        }
    }
    orig(queue, numLists, lists);
}

inline HRESULT __stdcall HookedPresent(IDXGISwapChain* swap, UINT sync, UINT flags) {
    auto& s = State();
    auto orig = s.origPresent;
    if (!orig) return DXGI_ERROR_INVALID_CALL;
    if (!s.firstPresentLogged) {
        s.firstPresentLogged = true;
        Log("dx12_overlay: Present hook fired (first invocation)");
    }
    if (!s.initialized) {
        InitDeviceResources(swap);
    }
    if (s.initialized) {
        RenderFrame(swap);
    }
    return orig(swap, sync, flags);
}

inline HRESULT __stdcall HookedResizeBuffers(IDXGISwapChain* swap, UINT bufferCount, UINT width,
                                             UINT height, DXGI_FORMAT format, UINT swapChainFlags) {
    auto& s = State();
    auto orig = s.origResize;
    if (!orig) return DXGI_ERROR_INVALID_CALL;
    if (s.initialized) {
        // ResizeBuffers fails outright while anything still references the back
        // buffers, and the GPU may still be reading ours, so drain first and drop
        // every device resource. The next Present rebuilds them against the new
        // size and format.
        FlushGpu();
        ReleaseDeviceResources();
    }
    return orig(swap, bufferCount, width, height, format, swapChainFlags);
}

// The two vtables this overlay patches, from a throwaway device, queue and swap
// chain of our own. Same probe technique as the D3D11 backend, with the extra
// step D3D12 forces: the swap chain is created against a command queue rather
// than a device, and that queue is also where ExecuteCommandLists is read from.
inline bool GetVTables(void**& outSwapChainVTable, void**& outQueueVTable) {
    WNDCLASSEXA wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = DefWindowProcA;
    wc.hInstance     = GetModuleHandleA(nullptr);
    wc.lpszClassName = "_CUL_Overlay12Probe";
    if (!RegisterClassExA(&wc)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    }
    HWND hwnd = CreateWindowA("_CUL_Overlay12Probe", "_probe", WS_POPUP, 0, 0, 16, 16,
                              nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) return false;

    ID3D12Device*       dev     = nullptr;
    ID3D12CommandQueue* queue   = nullptr;
    IDXGIFactory4*      factory = nullptr;
    IDXGISwapChain1*    swap    = nullptr;
    bool ok = false;

    if (SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev)))) {
        D3D12_COMMAND_QUEUE_DESC qd = {};
        qd.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
        qd.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        if (SUCCEEDED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue))) &&
            SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
            DXGI_SWAP_CHAIN_DESC1 scd = {};
            scd.Width       = 16;
            scd.Height      = 16;
            scd.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
            scd.SampleDesc  = {1, 0};
            scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            scd.BufferCount = 2;
            scd.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            if (SUCCEEDED(factory->CreateSwapChainForHwnd(queue, hwnd, &scd, nullptr,
                                                          nullptr, &swap)) && swap) {
                outSwapChainVTable = *reinterpret_cast<void***>(swap);
                outQueueVTable     = *reinterpret_cast<void***>(queue);
                ok = true;
            }
        }
    }

    if (swap)    swap->Release();
    if (factory) factory->Release();
    if (queue)   queue->Release();
    if (dev)     dev->Release();
    DestroyWindow(hwnd);
    return ok;
}

} // namespace detail12

inline DX12Overlay::~DX12Overlay() { Remove(); }

inline void SetDX12OverlayLogger(DX12LogFn fn) { detail12::State().logFn = fn; }

inline bool DX12Overlay::Install() {
    auto& s = detail12::State();

    if (m_hookInstalled) return true;
    if (s.hookInstalled) {
        // Refused rather than silently taken over, for the same reason the D3D11
        // backend refuses: the hooks and the callback slot are process-wide and
        // there is one of each, so a second instance claiming them blanks the
        // first and whichever is destroyed first tears down the other's state.
        detail12::Log("dx12_overlay: Install refused, another DX12Overlay in this "
                      "module already owns the hooks");
        return false;
    }

    void** swapVTable  = nullptr;
    void** queueVTable = nullptr;
    if (!detail12::GetVTables(swapVTable, queueVTable)) {
        detail12::Log("dx12_overlay: probe device/swap chain creation failed, so this "
                      "machine has no usable Direct3D 12 adapter");
        return false;
    }
    detail12::Log("dx12_overlay: swap chain and command queue vtables obtained");

    // IDXGISwapChain (DXGI 1.0): Present @ 8, ResizeBuffers @ 13.
    // ID3D12CommandQueue: IUnknown 0-2, ID3D12Object 3-6, ID3D12DeviceChild 7,
    // then UpdateTileMappings 8, CopyTileMappings 9, ExecuteCommandLists 10.
    s.presentTarget = swapVTable[8];
    s.resizeTarget  = swapVTable[13];
    s.executeTarget = queueVTable[10];

    if (MH_CreateHook(s.presentTarget, &detail12::HookedPresent,
                      reinterpret_cast<LPVOID*>(&s.origPresent)) != MH_OK) {
        detail12::Log("dx12_overlay: MH_CreateHook(Present) failed - a DX11 overlay in "
                      "this process may already own the shared DXGI vtable");
        return false;
    }
    if (MH_CreateHook(s.resizeTarget, &detail12::HookedResizeBuffers,
                      reinterpret_cast<LPVOID*>(&s.origResize)) != MH_OK) {
        detail12::Log("dx12_overlay: MH_CreateHook(ResizeBuffers) failed");
        MH_RemoveHook(s.presentTarget);
        return false;
    }
    if (MH_CreateHook(s.executeTarget, &detail12::HookedExecuteCommandLists,
                      reinterpret_cast<LPVOID*>(&s.origExecute)) != MH_OK) {
        detail12::Log("dx12_overlay: MH_CreateHook(ExecuteCommandLists) failed");
        MH_RemoveHook(s.presentTarget);
        MH_RemoveHook(s.resizeTarget);
        return false;
    }
    if (MH_EnableHook(s.presentTarget) != MH_OK ||
        MH_EnableHook(s.resizeTarget)  != MH_OK ||
        MH_EnableHook(s.executeTarget) != MH_OK) {
        detail12::Log("dx12_overlay: MH_EnableHook failed");
        MH_RemoveHook(s.presentTarget);
        MH_RemoveHook(s.resizeTarget);
        MH_RemoveHook(s.executeTarget);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(s.callbackMutex);
        s.callback = std::make_shared<const DX12RenderCallback>(m_callback);
    }
    s.hookInstalled = true;
    m_hookInstalled = true;
    detail12::Log("dx12_overlay: hooks enabled");
    return true;
}

inline void DX12Overlay::Remove() {
    if (!m_hookInstalled) return;

    auto& s = detail12::State();
    // Null-guarded: MH_ALL_HOOKS is NULL, so a null target here would disable
    // and remove every MinHook hook in the process, the mod's camera hook
    // included.
    if (s.presentTarget) { MH_DisableHook(s.presentTarget); MH_RemoveHook(s.presentTarget); }
    if (s.resizeTarget)  { MH_DisableHook(s.resizeTarget);  MH_RemoveHook(s.resizeTarget); }
    if (s.executeTarget) { MH_DisableHook(s.executeTarget); MH_RemoveHook(s.executeTarget); }

    detail12::FlushGpu();
    detail12::ReleaseDeviceResources();
    if (ID3D12CommandQueue* queue = s.queue.exchange(nullptr, std::memory_order_acq_rel)) {
        queue->Release();
    }
    {
        std::lock_guard<std::mutex> lock(s.callbackMutex);
        s.callback = nullptr;
    }
    // Targets AND trampolines cleared. See dx11_overlay.h: MH_RemoveHook returns
    // the trampoline to MinHook's pool while a thread may still be inside our
    // detour, and the null checks at the top of each detour are what stands
    // between that thread and a call through recycled memory.
    s.presentTarget = nullptr;
    s.resizeTarget  = nullptr;
    s.executeTarget = nullptr;
    s.origPresent   = nullptr;
    s.origResize    = nullptr;
    s.origExecute   = nullptr;
    s.hookInstalled = false;
    m_hookInstalled = false;
}

inline void DX12Overlay::SetRenderCallback(DX12RenderCallback cb) {
    m_callback = cb;
    auto& s = detail12::State();
    if (s.hookInstalled && !m_hookInstalled) {
        detail12::Log("dx12_overlay: SetRenderCallback ignored, another overlay owns the hooks");
        return;
    }
    std::lock_guard<std::mutex> lock(s.callbackMutex);
    s.callback = std::make_shared<const DX12RenderCallback>(std::move(cb));
}

#endif // CAMERAUNLOCK_DX12_OVERLAY_IMPLEMENTATION
// Non-implementation TUs see only the declarations above; method definitions
// live in the impl TU and resolve at link time.

} // namespace cameraunlock::rendering
