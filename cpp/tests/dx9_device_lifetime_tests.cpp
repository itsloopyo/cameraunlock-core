#include <d3d9.h>
#include <Windows.h>
#include <cstdio>
#include <cstdlib>

struct FakeComObject { void** table; };
FakeComObject probe{};
FakeComObject factory{};
void* factoryTable[17]{};
bool probeReleased = false;

HRESULT __stdcall FakeMethod() { return D3D_OK; }
ULONG __stdcall ReleaseFactory(IDirect3D9*) { return 0; }
ULONG __stdcall ReleaseProbe(IDirect3DDevice9*) {
    if (!VirtualFree(probe.table, 0, MEM_RELEASE)) std::abort();
    probe.table = nullptr;
    probeReleased = true;
    return 0;
}
HRESULT __stdcall CreateProbe(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD,
                              D3DPRESENT_PARAMETERS*, IDirect3DDevice9** result) {
    probe.table = static_cast<void**>(VirtualAlloc(nullptr, 4096,
                                                 MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!probe.table) std::abort();
    for (int i = 0; i < 119; ++i) probe.table[i] = reinterpret_cast<void*>(&FakeMethod);
    probe.table[2] = reinterpret_cast<void*>(&ReleaseProbe);
    *result = reinterpret_cast<IDirect3DDevice9*>(&probe);
    return D3D_OK;
}
IDirect3D9* WINAPI CreateFactory(UINT) {
    factoryTable[2] = reinterpret_cast<void*>(&ReleaseFactory);
    factoryTable[16] = reinterpret_cast<void*>(&CreateProbe);
    factory.table = factoryTable;
    return reinterpret_cast<IDirect3D9*>(&factory);
}

#define Direct3DCreate9 CreateFactory
#define CAMERAUNLOCK_DX9_OVERLAY_IMPLEMENTATION
#include "cameraunlock/rendering/dx9_overlay.h"
namespace rndr = cameraunlock::rendering;
void** capturedTable = nullptr;
int callbacks = 0;

void CheckTable(void** table) {
    for (int i = 0; i < 119; ++i) {
        void* expected = i == 2 ? reinterpret_cast<void*>(&ReleaseProbe)
                                : reinterpret_cast<void*>(&FakeMethod);
        if (table[i] != expected) {
            std::fprintf(stderr, "Wrong method at slot %d\n", i);
            std::exit(1);
        }
    }
}

void DeviceReady(void** table) {
    if (!probeReleased) std::abort();
    CheckTable(table);
    capturedTable = table;
    ++callbacks;
    std::puts("Callback can read all 119 method entries");
}

int main() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (MH_Initialize() != MH_OK) return 1;
    rndr::SetDX9OverlayLogger([](const char* line) { std::puts(line); std::fflush(stdout); });
    rndr::SetDX9DeviceReadyCallback(DeviceReady);
    rndr::DX9Overlay overlay;
    if (!overlay.Install() || callbacks != 1) return 2;
    CheckTable(capturedTable);
    overlay.Remove();
    if (MH_Uninitialize() != MH_OK) return 3;
    std::puts("PASS: device table remains readable after probe release and Install returns");
}
