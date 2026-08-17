#pragma once
#include "imgui.h"
#include <d3d12.h>
#include <dxgi1_4.h>
struct ImGui_ImplDX12_InitInfo {
    ID3D12Device* Device;
    ID3D12CommandQueue* CommandQueue;
    int NumFramesInFlight;
    DXGI_FORMAT RTVFormat;
    DXGI_FORMAT DSVFormat;
    ID3D12DescriptorHeap* SrvDescriptorHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE LegacySingleSrvCpuDescriptor;
    D3D12_GPU_DESCRIPTOR_HANDLE LegacySingleSrvGpuDescriptor;
};
inline bool ImGui_ImplDX12_Init(ImGui_ImplDX12_InitInfo*) { return true; }
inline void ImGui_ImplDX12_Shutdown() {}
inline void ImGui_ImplDX12_NewFrame() {}
inline void ImGui_ImplDX12_RenderDrawData(ImDrawData*, ID3D12GraphicsCommandList*) {}
