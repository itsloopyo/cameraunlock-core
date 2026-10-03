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
//     swap chain will not name the queue it was created against. It does hold
//     that queue's pointer, though, and where is measured at Install on a
//     throwaway swap chain whose queue is known. The game's swap chain is read
//     at the same place, and the answer is accepted once ExecuteCommandLists,
//     hooked purely to record the DIRECT queues in use, has seen that queue
//     submit. It has to be that queue: a game can run several DIRECT queues
//     (Starfield runs two), and a draw submitted on another is not ordered
//     against Present, lands after the flip and never reaches the screen. Hooks
//     installed mid-game see whichever queue submits next, which is why the
//     first one seen is not the answer. Until the queue is known the overlay
//     has nowhere to submit and draws nothing.
//   - **The swap chain under Streamline's.** In a game that loads NVIDIA
//     Streamline (sl.interposer.dll) every swap chain made in the process is
//     handed back wrapped, the throwaway one included, so hooks taken from it
//     land on the wrapper. The wrapper's back buffers are not the ones
//     presented: with frame generation on, Streamline presents the DXGI swap
//     chain underneath from a thread of its own, through a queue of its own,
//     and a draw into the wrapper's buffer never reaches the screen. With it
//     off the wrapper's Present runs twice a frame, nested, for two wrapper
//     objects. So Install asks the wrapper for the DXGI swap chain under it
//     and hooks that one: Present then arrives once per frame shown, generated
//     frames included, with the swap chain whose buffers are on screen.
//   - **Per-back-buffer state.** One command allocator per buffer, and a fence
//     value per buffer, because an allocator cannot be reset while the GPU is
//     still reading the commands it holds.
//   - **Present1 as well as Present.** A game can present through either, and
//     which one reaches the DXGI swap chain can change in the middle of a
//     session: Dying Light 2 calls Present1, and with frame generation on
//     Streamline turns that into Present calls from its own thread, so with it
//     switched off in the settings the frames arrive through Present1 alone.
//     Both are hooked and draw through the same path.
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
    //
    // Call it only once the game has created its own D3D12 device. The vtable
    // probe creates a throwaway device, and D3D12 hands an existing device back
    // rather than making a second one for the same adapter. In Far Cry 6's Steam
    // build, a probe run while the game was still creating its device (no hooks
    // installed, nothing drawn) left the game rendering posterised with blank UI
    // textures for the whole session.
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
#include "cameraunlock/rendering/held_pointer.h"
#include <atomic>
#include <cstdio>
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
using Present1_t = HRESULT (__stdcall*)(IDXGISwapChain*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
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

// More DIRECT queues than any engine seen so far runs. A queue past this is
// not recorded, and the overlay says so if it then cannot find the swap chain's.
inline constexpr int kMaxSeenQueues = 16;

// How much of the throwaway swap chain, and of each object it points at, is
// searched for its queue, and how many presents go by before an unknown queue is
// reported, or a lone DIRECT queue is taken as the answer.
inline constexpr size_t kSwapChainSearchBytes = 0x800;
inline constexpr size_t kInnerSearchBytes = 0x400;
inline constexpr int kQueueSettlePresents = 120;

// Where a swap chain keeps the queue it was created against: at `outer` in the
// swap chain object itself, or, when `indirect`, at `inner` in the object the
// pointer at `outer` leads to.
struct QueuePath {
    bool   known    = false;
    bool   indirect = false;
    size_t outer    = 0;
    size_t inner    = 0;
};

// The ways RenderFrame can drop a frame it had something to draw in.
enum DropReason {
    kDropOtherSwapChain, kDropNoSwapChain3, kDropBufferIndex, kDropFenceWait,
    kDropAllocatorReset, kDropListReset, kDropListClose, kDropNoExecute, kDropReasons
};

struct OverlayState {
    // Hooks
    bool  hookInstalled = false;
    void* presentTarget = nullptr;
    void* present1Target = nullptr;
    void* resizeTarget  = nullptr;
    void* executeTarget = nullptr;
    Present_t             origPresent = nullptr;
    Present1_t            origPresent1 = nullptr;
    ResizeBuffers_t       origResize  = nullptr;
    ExecuteCommandLists_t origExecute = nullptr;

    // Every DIRECT queue seen in the ExecuteCommandLists detour, in the order
    // seen. Atomic because that detour runs on whichever thread submits, and
    // AddRef'd on capture: a queue released underneath us would be submitted to
    // for the rest of the session.
    std::atomic<ID3D12CommandQueue*> seenQueues[kMaxSeenQueues] = {};
    std::atomic<int> seenQueueCount{0};
    std::mutex seenQueueMutex;

    // The one of those the swap chain presents through. Not a reference of its
    // own: seenQueues holds it.
    std::atomic<ID3D12CommandQueue*> queue{nullptr};
    int presentsUnresolved = 0;
    QueuePath queuePath;

    // Device resources
    bool                       initialized = false;
    // The swap chain they were built from. Compared, never called through.
    IDXGISwapChain*            swap        = nullptr;
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
    bool firstPresent1Logged = false;
    bool notD3D12Logged     = false;
    // One line per way a frame can go undrawn, and one for the first frame that
    // is drawn: an overlay that publishes and shows nothing is otherwise silent.
    bool queueUnresolvedLogged = false;
    bool firstDrawLogged    = false;
    bool dropLogged[kDropReasons] = {};
};

inline OverlayState& State() {
    static OverlayState s;
    return s;
}

inline void Log(const char* msg);

// Said once per reason: these run every frame, and the first occurrence is the
// diagnosis.
inline void LogDrop(DropReason reason, const char* msg) {
    auto& s = State();
    if (s.dropLogged[reason]) return;
    s.dropLogged[reason] = true;
    Log(msg);
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
    // The queue is read again with every rebuild. A game that rebuilds its swap
    // chain can come back presenting through another queue (Starfield does when
    // frame generation is switched in its settings), and the one resolved for
    // the old swap chain is still alive, since seenQueues holds a reference, so
    // a draw submitted on it is accepted and is not ordered against Present.
    s.queue.store(nullptr, std::memory_order_release);
    s.presentsUnresolved = 0;
    s.queueUnresolvedLogged = false;

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
    s.swap = swap;
    s.initialized = true;
    char line[160];
    std::snprintf(line, sizeof(line),
                  "dx12_overlay: device resources initialized for swap chain %p, %u buffers at %ux%u",
                  static_cast<void*>(swap), bufferCount, s.width, s.height);
    Log(line);
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
    s.swap = nullptr;
    s.initialized = false;
}

// Records a DIRECT queue the first time it submits. Runs on every submission
// until the swap chain's queue is known, so the already-seen check takes no lock.
inline void RecordQueue(ID3D12CommandQueue* queue) {
    auto& s = State();
    const int seen = s.seenQueueCount.load(std::memory_order_acquire);
    for (int i = 0; i < seen; ++i) {
        if (s.seenQueues[i].load(std::memory_order_relaxed) == queue) return;
    }
    if (queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT) return;

    std::lock_guard<std::mutex> lock(s.seenQueueMutex);
    const int count = s.seenQueueCount.load(std::memory_order_acquire);
    for (int i = 0; i < count; ++i) {
        if (s.seenQueues[i].load(std::memory_order_relaxed) == queue) return;
    }
    if (count == kMaxSeenQueues) return;
    queue->AddRef();
    s.seenQueues[count].store(queue, std::memory_order_release);
    s.seenQueueCount.store(count + 1, std::memory_order_release);
}

// How many bytes from `address` on can be read, up to `wanted`: the swap chain
// object's size is not known, so the search stops where its memory does.
inline size_t ReadableBytes(const void* address, size_t wanted) {
    const auto start = reinterpret_cast<uintptr_t>(address);
    uintptr_t at = start;
    while (at < start + wanted) {
        MEMORY_BASIC_INFORMATION info = {};
        if (VirtualQuery(reinterpret_cast<const void*>(at), &info, sizeof(info)) != sizeof(info)) break;
        const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE;
        if (info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD) || !(info.Protect & readable)) break;
        at = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
    }
    return at <= start ? 0 : (at - start < wanted ? at - start : wanted);
}

// Where `swap` holds `queue`, searched in the swap chain object and then one
// pointer away from it. Run on the throwaway pair Install creates, where the
// answer is known.
inline QueuePath FindQueuePath(IDXGISwapChain* swap, ID3D12CommandQueue* queue) {
    QueuePath path;
    const void* candidates[1] = {queue};
    const size_t bytes = ReadableBytes(swap, kSwapChainSearchBytes);
    if (FindHeldPointer(swap, bytes, candidates, 1, &path.outer) == 0) {
        path.known = true;
        return path;
    }
    const auto* base = reinterpret_cast<const unsigned char*>(swap);
    for (size_t at = 0; at + sizeof(void*) <= bytes; at += sizeof(void*)) {
        const void* inner = nullptr;
        std::memcpy(&inner, base + at, sizeof(inner));
        if (reinterpret_cast<uintptr_t>(inner) % alignof(void*) != 0) continue;
        if (FindHeldPointer(inner, ReadableBytes(inner, kInnerSearchBytes), candidates, 1,
                            &path.inner) == 0) {
            path.known = true;
            path.indirect = true;
            path.outer = at;
            return path;
        }
    }
    return path;
}

// What the swap chain holds where the path says its queue is, or null when the
// path does not lead anywhere readable.
inline const void* ReadQueuePath(IDXGISwapChain* swap, const QueuePath& path) {
    const void* at = swap;
    if (ReadableBytes(at, path.outer + sizeof(void*)) < path.outer + sizeof(void*)) return nullptr;
    const void* value = nullptr;
    std::memcpy(&value, static_cast<const unsigned char*>(at) + path.outer, sizeof(value));
    if (!path.indirect) return value;
    if (ReadableBytes(value, path.inner + sizeof(void*)) < path.inner + sizeof(void*)) return nullptr;
    std::memcpy(&value, static_cast<const unsigned char*>(value) + path.inner, sizeof(value));
    return value;
}

// The queue this swap chain presents through, or null while it is not known.
// Called from Present until it answers, since a queue is only trusted once it
// has been seen submitting.
inline ID3D12CommandQueue* ResolveSwapChainQueue(IDXGISwapChain* swap) {
    auto& s = State();
    const int seen = s.seenQueueCount.load(std::memory_order_acquire);
    const void* held = s.queuePath.known ? ReadQueuePath(swap, s.queuePath) : nullptr;
    char line[220];
    for (int i = 0; held && i < seen; ++i) {
        ID3D12CommandQueue* queue = s.seenQueues[i].load(std::memory_order_acquire);
        if (queue != held) continue;
        std::snprintf(line, sizeof(line),
                      "dx12_overlay: swap chain %p presents through DIRECT queue %d of %d seen",
                      static_cast<void*>(swap), i + 1, seen);
        Log(line);
        s.queue.store(queue, std::memory_order_release);
        return queue;
    }

    if (++s.presentsUnresolved < kQueueSettlePresents) return nullptr;
    // A swap chain whose queue cannot be read still has only one queue it can
    // be, when only one DIRECT queue exists.
    if (!s.queuePath.known && seen == 1) {
        Log("dx12_overlay: where a swap chain keeps its queue could not be measured, so the one "
            "DIRECT queue in use is taken as this swap chain's");
        ID3D12CommandQueue* queue = s.seenQueues[0].load(std::memory_order_acquire);
        s.queue.store(queue, std::memory_order_release);
        return queue;
    }
    if (!s.queueUnresolvedLogged) {
        s.queueUnresolvedLogged = true;
        std::snprintf(line, sizeof(line),
                      "dx12_overlay: nothing is drawn, the swap chain's queue is not known: %d DIRECT "
                      "queues seen, and %s",
                      seen,
                      !s.queuePath.known ? "where a swap chain keeps its queue could not be measured"
                      : held ? "the queue the swap chain holds is not one of them"
                             : "the swap chain holds no queue where the measured one did");
        Log(line);
    }
    return nullptr;
}

inline void RenderFrame(IDXGISwapChain* swap) {
    auto& s = State();
    if (!s.initialized) return;
    if (s.width == 0 || s.height == 0) return;
    // The back buffers, their index and the queue are all this swap chain's.
    if (swap != s.swap) {
        LogDrop(kDropOtherSwapChain, "dx12_overlay: frame dropped, this Present is for a swap chain "
                                     "other than the one the overlay was built from");
        return;
    }

    ID3D12CommandQueue* queue = s.queue.load(std::memory_order_acquire);
    if (!queue) queue = ResolveSwapChainQueue(swap);
    if (!queue) return;

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
    if (FAILED(swap->QueryInterface(IID_PPV_ARGS(&swap3))) || !swap3) {
        LogDrop(kDropNoSwapChain3, "dx12_overlay: frame dropped, the swap chain gave no IDXGISwapChain3");
        return;
    }
    const UINT index = swap3->GetCurrentBackBufferIndex();
    swap3->Release();
    if (index >= s.frames.size()) {
        LogDrop(kDropBufferIndex, "dx12_overlay: frame dropped, the back buffer index is past the buffers captured");
        return;
    }

    FrameContext& f = s.frames[index];
    // The allocator still holds the commands of the last frame drawn into this
    // buffer. Resetting it before the GPU is done with them corrupts the list
    // being executed, which is the classic D3D12 overlay crash.
    if (!WaitForFence(f.fenceValue)) {
        LogDrop(kDropFenceWait, "dx12_overlay: frame dropped, the fence for this back buffer did not complete");
        return;
    }

    std::memcpy(s.vbCpu, verts.data(), sizeof(OverlayVertex) * needed);

    if (FAILED(f.allocator->Reset())) {
        LogDrop(kDropAllocatorReset, "dx12_overlay: frame dropped, command allocator Reset failed");
        return;
    }
    if (FAILED(s.cmdList->Reset(f.allocator, s.pso))) {
        LogDrop(kDropListReset, "dx12_overlay: frame dropped, command list Reset failed");
        return;
    }

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

    if (FAILED(s.cmdList->Close())) {
        LogDrop(kDropListClose, "dx12_overlay: frame dropped, command list Close failed");
        return;
    }

    ID3D12CommandList* lists[] = {s.cmdList};
    // The ORIGINAL, not the detour: our own submission has no queue to learn and
    // going back through the hook only adds a branch to every frame. Read once
    // and null-checked, because Remove() clears it and this runs on the render
    // thread - see the same guard at the top of each detour.
    auto execute = s.origExecute;
    if (!execute) {
        LogDrop(kDropNoExecute, "dx12_overlay: frame dropped, the ExecuteCommandLists hook is gone");
        return;
    }
    execute(queue, 1, lists);
    if (!s.firstDrawLogged) {
        s.firstDrawLogged = true;
        char line[160];
        std::snprintf(line, sizeof(line),
                      "dx12_overlay: first frame drawn, %u vertices into back buffer %u of %zu at %ux%u",
                      needed, index, s.frames.size(), s.width, s.height);
        Log(line);
    }

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

    if (!s.queue.load(std::memory_order_acquire)) RecordQueue(queue);
    orig(queue, numLists, lists);
}

// Set while this thread is inside one of the two present detours, so a DXGI
// that implements one present through the other draws the overlay once.
inline thread_local bool t_presenting = false;

inline void DrawOnPresent(IDXGISwapChain* swap, const char* firstCallLine) {
    auto& s = State();
    if (!s.firstPresentLogged) {
        s.firstPresentLogged = true;
        Log(firstCallLine);
    }
    if (!s.initialized) {
        InitDeviceResources(swap);
    }
    if (s.initialized) {
        RenderFrame(swap);
    }
}

inline HRESULT __stdcall HookedPresent(IDXGISwapChain* swap, UINT sync, UINT flags) {
    auto& s = State();
    auto orig = s.origPresent;
    if (!orig) return DXGI_ERROR_INVALID_CALL;
    if (t_presenting) return orig(swap, sync, flags);
    DrawOnPresent(swap, "dx12_overlay: Present hook fired (first invocation)");
    t_presenting = true;
    const HRESULT result = orig(swap, sync, flags);
    t_presenting = false;
    return result;
}

inline HRESULT __stdcall HookedPresent1(IDXGISwapChain* swap, UINT sync, UINT flags,
                                        const DXGI_PRESENT_PARAMETERS* parameters) {
    auto& s = State();
    auto orig = s.origPresent1;
    if (!orig) return DXGI_ERROR_INVALID_CALL;
    if (t_presenting) return orig(swap, sync, flags, parameters);
    if (!s.firstPresent1Logged) {
        s.firstPresent1Logged = true;
        Log("dx12_overlay: Present1 hook fired (first invocation)");
    }
    DrawOnPresent(swap, "dx12_overlay: the first present to arrive came through Present1");
    t_presenting = true;
    const HRESULT result = orig(swap, sync, flags, parameters);
    t_presenting = false;
    return result;
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

// Streamline's wrappers answer this interface id with the object they wrap
// (StreamlineRetrieveBaseInterface in its SDK). Anything else refuses it.
inline constexpr GUID kStreamlineBaseInterface =
    {0xADEC44E2, 0x61F0, 0x45C3, {0xAD, 0x9F, 0x1B, 0x37, 0x37, 0x92, 0x84, 0xFF}};
inline constexpr int kMaxWrapperLayers = 4;

// The DXGI swap chain under `swap`, which is `swap` itself where nothing wraps
// it. Not a reference of its own: the wrapper holds what it wraps.
inline IDXGISwapChain* NativeSwapChain(IDXGISwapChain* swap) {
    for (int layer = 0; layer < kMaxWrapperLayers; ++layer) {
        void* base = nullptr;
        if (FAILED(swap->QueryInterface(kStreamlineBaseInterface, &base)) || !base) return swap;
        static_cast<IUnknown*>(base)->Release();
        if (base == swap) return swap;
        swap = static_cast<IDXGISwapChain*>(base);
    }
    return swap;
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
                IDXGISwapChain* native = NativeSwapChain(swap);
                if (native != swap) {
                    Log("dx12_overlay: swap chains here are wrapped by Streamline, so the hooks go on "
                        "the DXGI swap chain under the wrapper");
                }
                outSwapChainVTable = *reinterpret_cast<void***>(native);
                outQueueVTable     = *reinterpret_cast<void***>(queue);
                ok = true;
                State().queuePath = FindQueuePath(native, queue);
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
    {
        const detail12::QueuePath& path = s.queuePath;
        char line[160];
        if (!path.known) {
            std::snprintf(line, sizeof(line), "dx12_overlay: a swap chain's queue was not found in it");
        } else if (path.indirect) {
            std::snprintf(line, sizeof(line),
                          "dx12_overlay: a swap chain keeps its queue at +0x%zX of the object at its +0x%zX",
                          path.inner, path.outer);
        } else {
            std::snprintf(line, sizeof(line), "dx12_overlay: a swap chain keeps its queue at +0x%zX",
                          path.outer);
        }
        detail12::Log(line);
    }

    // IDXGISwapChain (DXGI 1.0): Present @ 8, ResizeBuffers @ 13.
    // IDXGISwapChain1 (DXGI 1.2): Present1 @ 22.
    // ID3D12CommandQueue: IUnknown 0-2, ID3D12Object 3-6, ID3D12DeviceChild 7,
    // then UpdateTileMappings 8, CopyTileMappings 9, ExecuteCommandLists 10.
    s.presentTarget = swapVTable[8];
    s.present1Target = swapVTable[22];
    s.resizeTarget  = swapVTable[13];
    s.executeTarget = queueVTable[10];

    if (MH_CreateHook(s.presentTarget, &detail12::HookedPresent,
                      reinterpret_cast<LPVOID*>(&s.origPresent)) != MH_OK) {
        detail12::Log("dx12_overlay: MH_CreateHook(Present) failed - a DX11 overlay in "
                      "this process may already own the shared DXGI vtable");
        return false;
    }
    if (MH_CreateHook(s.present1Target, &detail12::HookedPresent1,
                      reinterpret_cast<LPVOID*>(&s.origPresent1)) != MH_OK) {
        detail12::Log("dx12_overlay: MH_CreateHook(Present1) failed");
        MH_RemoveHook(s.presentTarget);
        return false;
    }
    if (MH_CreateHook(s.resizeTarget, &detail12::HookedResizeBuffers,
                      reinterpret_cast<LPVOID*>(&s.origResize)) != MH_OK) {
        detail12::Log("dx12_overlay: MH_CreateHook(ResizeBuffers) failed");
        MH_RemoveHook(s.presentTarget);
        MH_RemoveHook(s.present1Target);
        return false;
    }
    if (MH_CreateHook(s.executeTarget, &detail12::HookedExecuteCommandLists,
                      reinterpret_cast<LPVOID*>(&s.origExecute)) != MH_OK) {
        detail12::Log("dx12_overlay: MH_CreateHook(ExecuteCommandLists) failed");
        MH_RemoveHook(s.presentTarget);
        MH_RemoveHook(s.present1Target);
        MH_RemoveHook(s.resizeTarget);
        return false;
    }
    if (MH_EnableHook(s.presentTarget) != MH_OK ||
        MH_EnableHook(s.present1Target) != MH_OK ||
        MH_EnableHook(s.resizeTarget)  != MH_OK ||
        MH_EnableHook(s.executeTarget) != MH_OK) {
        detail12::Log("dx12_overlay: MH_EnableHook failed");
        MH_RemoveHook(s.presentTarget);
        MH_RemoveHook(s.present1Target);
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
    if (s.present1Target) { MH_DisableHook(s.present1Target); MH_RemoveHook(s.present1Target); }
    if (s.resizeTarget)  { MH_DisableHook(s.resizeTarget);  MH_RemoveHook(s.resizeTarget); }
    if (s.executeTarget) { MH_DisableHook(s.executeTarget); MH_RemoveHook(s.executeTarget); }

    detail12::FlushGpu();
    detail12::ReleaseDeviceResources();
    s.queue.store(nullptr, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(s.seenQueueMutex);
        const int seen = s.seenQueueCount.exchange(0, std::memory_order_acq_rel);
        for (int i = 0; i < seen; ++i) {
            if (ID3D12CommandQueue* queue = s.seenQueues[i].exchange(nullptr, std::memory_order_acq_rel)) {
                queue->Release();
            }
        }
    }
    s.presentsUnresolved = 0;
    {
        std::lock_guard<std::mutex> lock(s.callbackMutex);
        s.callback = nullptr;
    }
    // Targets AND trampolines cleared. See dx11_overlay.h: MH_RemoveHook returns
    // the trampoline to MinHook's pool while a thread may still be inside our
    // detour, and the null checks at the top of each detour are what stands
    // between that thread and a call through recycled memory.
    s.presentTarget = nullptr;
    s.present1Target = nullptr;
    s.resizeTarget  = nullptr;
    s.executeTarget = nullptr;
    s.origPresent   = nullptr;
    s.origPresent1  = nullptr;
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
